# TextureDefrag Upstream Provenance

This directory vendors the TextureDefrag reference implementation for:

**Texture Defragmentation for Photo-Reconstructed 3D Models**  
Andrea Maggiordomo, Paolo Cignoni, Marco Tarini. Eurographics 2021.

Upstream repository: https://github.com/maggio-a/texture-defrag

The vendored code was imported from the MeshLab reference copy in:

`.reference/meshlab/src/meshlabplugins/filter_texture_defragmentation/TextureDefragmentation`

## QMeshLab Adaptations

The algorithmic source is kept intentionally close to upstream so it can be compared when a newer upstream version is integrated.

Adapted files:

- `src/mesh.cpp`: removed standalone mesh file IO behavior from the build path. QMeshLab feeds the algorithm from the in-memory `Document` mesh model.
- `src/texture_object.cpp`: changed `TextureObject` from an OpenGL texture owner into a QImage/size container. GPU upload is left to QMeshLab renderer backends.
- `src/texture_rendering.cpp`: replaced the original OpenGL atlas rasterizer with a QImage renderer behind the same `RenderTexture` API. This keeps the backend boundary narrow for a future QRhi renderer.
- `src/mesh_graph.cpp`: removed the stale OpenGL utility include.

Removed files:

- `src/gl_utils.cpp`
- `src/gl_utils.h`

Those files were only needed by the original OpenGL renderer and standalone importer path. QMeshLab does not build or use them.
