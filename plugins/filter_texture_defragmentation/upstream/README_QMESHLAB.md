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

### Merging islands chosen by hand

The `SmallIslandRemover` variant of `FilterType` is itself a QMeshLab addition (see
`seam_remover.h`, and the median-border threshold in `ComputeCost`). On top of it, the same
merger can take its candidates from the mesh's face selection instead of from a size
threshold, which is what the filter's *Islands to merge* option switches between. Three
changes, all marked `QMeshLab:` in the source:

- `src/mesh_graph.{h,cpp}`: `FaceGroup` caches whether any of its faces is selected, folded
  into the existing `UpdateCache` sweep, and exposes `AnySelectedFace()`. The plugin copies
  the selection onto the faces when it builds the defrag mesh, so the mark travels with the
  face -- the pipeline cleans and compacts before the chart graph is built, so index
  alignment with the source layer is not something to rely on.
- `src/seam_remover.{h,cpp}`: `AlgoParameters::mergeSelectedIslands` picks which rule gates
  a merge in `ComputeCost`. Both rules have the same shape: reject only when *neither* chart
  qualifies, so a qualifying island dissolves into an unqualifying neighbour.
- `src/seam_remover.cpp`: two things are needed to make "merge what I picked" mean it.
  `FaceGroup::ClearFaceSelection()` is called as a merge is committed, because the union of
  two charts inherits the mark and a single selected face would otherwise swallow the whole
  connected atlas. And eligibility is re-checked where the merge is executed, not only where
  its cost was computed -- costs are worked out up front, so a pair queued while the mark was
  live would otherwise still run after the mark had been consumed. A pair that no longer
  qualifies has its cost retired to infinity rather than being skipped, because the greedy
  loop terminates on an infinite queue top and a bare `continue` leaves it churning.

Measured on an eight-island layer with one island selected: one merge, seven islands left.
Before the first of those two changes the same input collapsed to a single island; before the
second it lost four.

Removed files:

- `src/gl_utils.cpp`
- `src/gl_utils.h`

Those files were only needed by the original OpenGL renderer and standalone importer path. QMeshLab does not build or use them.
