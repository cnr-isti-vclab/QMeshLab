# Filter Organization and Naming

This document is a draft proposal for making QMeshLab filters easier to browse,
name, document, script, and maintain. It is intentionally written as a design
discussion document: the taxonomy below should be edited as we learn which names
feel natural in daily use.

See also: [Adding a Filter](adding_a_filter.md) (practical how-to),
[Architecture](architecture.md) and [Data Model](data_model.md).

## Goals

- Make the filter browser predictable: users should know where to look for an
  operation before they know the exact filter name.
- Make filter names describe the observable result of the operation.
- Keep Python names explicit, coherent, and readable.
- Avoid hiding persistent mesh changes behind names that sound like temporary
  visualization changes.
- Separate framework concerns from filter-specific algorithms.
- Prefer a clean, coherent taxonomy over preserving legacy script names.

## Current Issues

The current filter set is partly inherited from MeshLab naming and partly shaped
by incremental QMeshLab ports. That gives us working coverage, but not always a
coherent vocabulary.

Common issues:

- Some filters named `Colorize` actually compute quality/scalar values.
- Some filters both compute data and bake colors, which makes their side effects
  harder to predict.
- Similar parameters use slightly different names across plugins.
- Some plugin names mirror implementation provenance rather than user intent.
- Some Python names are algorithm-family names while others are action names.
- Menu categories mix operation families, data domains, and plugin provenance.
- Some defaults are document-dependent, so script generation and documentation
  need to be clear about when defaults are evaluated.

## Guiding Principle

A filter name should answer this question:

> What will be different in the document after this operation completes?

If a filter only computes a scalar field, the name should say `Compute`. If it
writes vertex colors, the name should say `Colorize` or `Bake`. If it creates a
new mesh, the name should say `Create`, `Generate`, or `Reconstruct`. If it
changes only the selection, the name should say `Select`.

## Proposed Top-Level Families

These families are meant to guide menu organization, display names, and Python
name prefixes. They do not have to map one-to-one to C++ plugin directories.

### Create / Generate

Creates new document data from parameters, from an existing mesh, or from other
layers.

Use for:

- primitive mesh creation
- synthetic point clouds or scalar fields
- reconstruction filters that add a new mesh
- atlas/image generation when the result is a new texture or layer

Preferred verbs:

- `Create` for simple geometric primitives or direct construction
- `Generate` for algorithmic outputs from parameters or existing data
- `Reconstruct` for surface reconstruction from points/ranges/volumes

Python prefix examples:

- `create_box`
- `create_sphere`
- `generate_noisy_isosurface`
- `generate_marching_cubes_apss`
- `reconstruct_surface_screened_poisson`

Draft direction:

- Reconstruction filters should use `reconstruct_*` names when that is the
  clearest description of the operation. Historical names should not drive the
  taxonomy.

### Clean / Repair

Fixes topological or geometric defects, removes invalid data, or normalizes mesh
storage.

Use for:

- duplicate vertex/face removal
- zero-area face removal
- non-manifold repair
- isolated component removal
- T-vertex and border repairs

Preferred verbs:

- `Remove` for destructive deletion
- `Repair` for operations that preserve intent while changing connectivity
- `Merge` for welding or combining equivalent data
- `Snap` for geometric border/vertex alignment

Python prefix examples:

- `remove_duplicate_vertices`
- `remove_zero_area_faces`
- `repair_non_manifold_edges`
- `merge_close_vertices`

Framework note:

- Generic compaction and unreferenced-vertex cleanup should be framework policy
  when possible, not a repeated per-filter option.

### Select

Changes vertex/face/mesh selection state without changing geometry or attributes.

Use for:

- selecting by geometry, quality, border, topology, visibility, or predicates
- expanding, shrinking, inverting, clearing, or combining selections

Preferred verbs:

- `Select`
- `Clear Selection`
- `Invert Selection`
- `Expand Selection`

Python prefix examples:

- `select_faces_by_quality`
- `select_non_manifold_edges`
- `clear_selection`

Framework note:

- Selection-only filters are good candidates for compact undo deltas rather than
  full document snapshots.

### Compute

Computes mesh attributes or analysis values without directly baking display
colors.

Use for:

- normals
- curvature
- quality/scalar values
- distances
- ambient occlusion values
- geodesics
- measurements stored as attributes or reported as output

Preferred verbs:

- `Compute`
- `Estimate` when the result is approximate or statistical
- `Measure` when the main result is informational

Python prefix examples:

- `compute_vertex_normals`
- `compute_face_normals`
- `compute_geodesic_distance_from_selection`
- `compute_ambient_occlusion`
- `compute_apss_curvature`

Important rule:

- A filter that computes quality should not bake vertex/face color by default.
  It should compute the attribute and optionally return a visualization hint so
  the UI can show quality coloring automatically.

### Visualize / Bake Color

Controls display or converts existing data into persistent colors.

Use for:

- baking current quality visualization into vertex/face color
- mapping scalar values to colors as a persistent data change
- setting visualization hints that do not modify mesh attributes

Preferred verbs:

- `Visualize` for view-only state or hints
- `Colorize` for persistent color writes
- `Bake` when converting a current view mapping into mesh data

Python prefix examples:

- `colorize_by_vertex_quality`
- `colorize_by_face_quality`
- `bake_quality_to_vertex_color`

Open question:

- Should pure view-state operations be filters, interactive tools, or render
  actions? They are useful in scripts, but they do not modify mesh data.

### Transform

Changes geometry positions, layer transforms, camera poses, or coordinate
systems.

Use for:

- translation, rotation, scaling
- applying/freeze transforms
- axis flips/swaps
- alignment matrix application
- camera transform operations

Preferred verbs:

- `Transform`
- `Apply`
- `Freeze`
- `Align`

Python prefix examples:

- `transform_translate`
- `transform_rotate`
- `transform_scale`
- `apply_matrix`
- `freeze_matrix`

Open question:

- Should transform filters operate on vertex coordinates, layer transforms, or
  both? Names should make this explicit.

### Remesh / Simplify / Subdivide

Changes mesh tessellation while preserving the intended surface.

Use for:

- simplification
- isotropic remeshing
- subdivision
- edge flips/collapses/splits
- solid wireframe conversion

Preferred verbs:

- `Simplify`
- `Remesh`
- `Subdivide`
- `Refine`

Python prefix examples:

- `simplification_quadric_edge_collapse`
- `remesh_isotropic_explicit`
- `subdivision_loop`
- `generate_solid_wireframe`

Naming note:

- Existing MeshLab/PyMeshLab names such as `simplification_*` are recognizable,
  but they should be kept only when they remain the clearest QMeshLab names.
  The taxonomy should not be constrained by historical naming.

### Parametrize / UV

Creates or modifies texture coordinates and atlas layouts.

Use for:

- UV unwrapping
- atlas packing
- xatlas integration
- wedge texture coordinate transfer or cleanup
- UV scaling/normalization

Preferred verbs:

- `Parametrize`
- `Unwrap`
- `Pack`
- `Generate UV`

Python prefix examples:

- `parametrization_voronoi_atlas`
- `parametrize_xatlas`
- `pack_uv_atlas`

Open question:

- Should the user-facing menu be `Parametrization` or `UV / Parametrization`?
  The latter may be easier for non-MeshLab users.

### Texture

Reads, writes, converts, transfers, or generates texture images associated with
mesh layers.

Use for:

- setting texture images
- transfer between texture and vertex/face attributes
- baking attributes to textures
- normal-map conversion
- texture defragmentation

Preferred verbs:

- `Set`
- `Transfer`
- `Bake`
- `Convert`
- `Defragment`

Python prefix examples:

- `set_texture`
- `transfer_texture_to_vertex_color`
- `transfer_vertex_color_to_texture`
- `convert_object_normal_map_to_tangent_space`
- `defragment_texture`

Parameter rule:

- Texture input/output parameters should use the shared texture selector
  parameter types. Filters should not invent one-off string/file parameters for
  mesh-owned textures unless the operation truly needs a file path.

### Transfer / Project

Moves data between meshes, layers, rasters, textures, or attribute domains.

Use for:

- raster color projection to mesh
- vertex-to-texture and texture-to-vertex transfer
- attribute transfer between meshes
- camera/raster projection operations

Preferred verbs:

- `Transfer` when data moves between existing domains
- `Project` when camera/raster projection is central to the operation
- `Bake` when the result becomes persistent image/color data

Python prefix examples:

- `transfer_vertex_attributes_to_texture`
- `transfer_texture_to_vertex_color`
- `project_current_raster_color_to_mesh`

Open question:

- Should raster projection filters live under `Raster`, `Texture`, or
  `Transfer / Projection`? The current `Raster` menu is technically accurate,
  but `Transfer / Projection` may be easier to discover.

### Measure / Inspect

Reports information, statistics, histograms, or diagnostics. Usually does not
modify mesh data.

Use for:

- bounding box and topology measures
- geometric measures
- quality histograms
- distance statistics
- layer/material/texture diagnostics

Preferred verbs:

- `Measure`
- `Inspect`
- `Report`

Python prefix examples:

- `compute_geometric_measures`
- `compute_face_quality_histogram`
- `inspect_mesh_attributes`

Framework note:

- Information-only filters should not create undo nodes unless they modify
  document state.

### Layer / Document

Changes document organization rather than mesh data.

Use for:

- duplicate layer
- merge layers
- split selected faces to a new layer
- rename/reorder/select layers
- render snapshot to raster layer

Preferred verbs:

- `Duplicate`
- `Merge`
- `Split`
- `Extract`
- `Add`
- `Remove`

Python prefix examples:

- `duplicate_current_mesh`
- `merge_visible_meshes`
- `split_selected_faces_to_new_mesh`

Open question:

- Some layer operations feel more like document commands than filters. Keeping
  them as filters is useful for undo/script/history, but the UI could present
  them in a document/layer command area.

### Camera / Render

Changes camera state, render settings, or creates render outputs.

Use for:

- camera positioning
- applying camera/render-state JSON
- rendering snapshots
- creating raster layers from render state

Preferred verbs:

- `Set Camera`
- `Apply Camera`
- `Render`
- `Snapshot`

Python prefix examples:

- `camera_set_view`
- `render_from_render_state_json`

Open question:

- These filters bridge document and view state. Their descriptors should be
  explicit about whether they modify the document, the active view, or both.

## Naming Rules

### Display Names

Display names should be readable and action-oriented.

Recommended pattern:

```text
Verb: Object / Method
```

Examples:

- `Simplification: Quadric Edge Collapse`
- `Compute: Vertex Normals`
- `Texture: Transfer Vertex Color to Texture`
- `Select: Non-Manifold Edges`
- `Repair: Non-Manifold Vertices`

Avoid:

- implementation-only names when a user-facing action is clearer
- `Colorize` for filters that only compute quality
- vague verbs such as `Process`, `Apply`, or `Filter` without an object
- names that hide whether the filter creates a new mesh or modifies the current
  mesh in place

### Python Names

Python names should be lowercase, snake_case, and explicit.

Recommended pattern:

```text
family_object_method
```

Examples:

- `compute_vertex_normals`
- `compute_geodesic_distance_from_selection`
- `simplification_quadric_edge_collapse`
- `transfer_vertex_color_to_texture`
- `convert_object_normal_map_to_tangent_space`

Policy:

- Choose the canonical Python name from the taxonomy, not from MeshLab legacy.
- Rename Python names when the new name is materially clearer.
- Avoid keeping multiple canonical names for the same operation.
- Add temporary aliases only if we later discover a concrete release-management
  need.

### Descriptor IDs

Descriptor `id` values are internal identifiers, but they should still be
coherent and aligned with the canonical operation name.

Recommended:

- rename ids when the current id is misleading or blocks a cleaner taxonomy
- use ids for implementation routing, not user education
- keep ids unchanged only when the existing id is already clear enough

### Menu Paths

Menu paths should help browsing. They can differ from plugin directory names.

Proposed top-level menu paths:

- `Create`
- `Clean / Repair`
- `Select`
- `Compute`
- `Color / Visualization`
- `Transform`
- `Remeshing`
- `Simplification`
- `Subdivision`
- `Parametrization / UV`
- `Texture`
- `Transfer / Projection`
- `Measure / Inspect`
- `Layer / Document`
- `Camera / Render`

Open question:

- Should `Simplification`, `Remeshing`, and `Subdivision` be separate top-level
  menus, or grouped under one `Meshing`/`Geometry` menu?

## Parameter Naming Rules

Parameters should be consistent across filters, especially for script users.

Recommended common names:

| Concept | Preferred id |
|---|---|
| source mesh | `sourceMesh` |
| target mesh | `targetMesh` |
| reference mesh | `referenceMesh` |
| control mesh | `controlMesh` |
| proxy mesh | `proxyMesh` |
| source texture | `sourceTexture` |
| target texture | `targetTexture` |
| output texture | `outputTexture` |
| selected-only toggle | `selectedOnly` |
| preserve boundary | `preserveBoundary` |
| preserve topology | `preserveTopology` |
| update normals | `updateNormals` |
| random seed | `randomSeed` |
| iteration count | `iterations` |
| quality threshold | `qualityThreshold` |
| distance threshold | `distanceThreshold` |

Current naming issue:

- Many ported filters use MeshLab-style ids such as `TargetFaceNum`,
  `QualityThr`, or `PreserveBoundary`. These should be replaced by coherent
  QMeshLab parameter ids when we do the naming pass.

Open question:

- Do we want descriptor support for temporary parameter aliases during a
  transition, or should the cleanup be a direct rename? Since script
  compatibility is not a priority, direct rename should be the default answer
  unless a specific problem appears.

## Defaults and Script Generation

Some defaults are static, such as `0`, `false`, or `"none"`. Others are dynamic
tokens resolved from the current document state, such as:

- current mesh index
- other mesh index
- bounding-box diagonal
- face count
- selected face count
- quality min/max
- hardware thread count

For compact Python generation, default comparison must use the descriptor state
from the moment the filter was invoked. Recomputing defaults after the filter has
run can produce wrong compact scripts. For example, quadric simplification uses a
target face count default derived from the current face count; after the filter
modifies the mesh, that default is no longer the same.

Policy:

- The filter panel's `Copy Python call` and the action-history compact script
  export must use the same formatting function.
- Action history should store both the full Python call and compact Python call
  at filter execution time.
- Full export should prefer the stored full call.
- Compact export should prefer the stored compact call.
- Reconstructing a call later should be treated as a fallback for old history
  entries only.

## Filter Side Effects

Every filter descriptor should make side effects explicit through:

- input domain
- output domain
- output-modifies codes
- input preparation requirements
- parameter-specific mesh preparation
- cleanup policy
- visualization hints

Suggested side-effect categories:

- `InformationOnly`
- `SelectionOnly`
- `ModifyCurrentMesh`
- `ModifyNamedMesh`
- `NewMesh`
- `NewTexture`
- `NewRaster`
- `ModifyDocumentStructure`
- `ModifyViewState`

Open question:

- Should these categories be formalized beyond the existing input/output domain
  and output-modifies fields?

## Migration Strategy

1. Document the taxonomy and agree on the vocabulary.
2. Audit all existing filters and assign each one a proposed family.
3. Choose canonical display names, Python names, descriptor ids, and parameter
   ids together.
4. Update menu paths and descriptors in one coordinated pass.
5. Update generated docs to show the canonical names.
6. Add temporary aliases only for cases where we explicitly decide the short-term
   release-management benefit is worth the extra complexity.
7. Remove stale names from user-facing docs once the new taxonomy is in place.

## Candidate Audit Table

This table is a starting point for review, not a final classification.

| Current area/plugin | Likely family | Notes |
|---|---|---|
| `filter_basic` | Create, Transform | Contains both primitive/generator-style and basic mesh operations. |
| `filter_clean` | Clean / Repair, Reconstruct | Ball pivoting may belong under Reconstruct rather than Cleaning. |
| `filter_meshing` | Remesh, Simplify, Subdivide, Transform, Compute | Very broad; likely needs the most careful menu split. |
| `filter_select` | Select | Should stay focused on selection-only behavior. |
| `filter_measure` | Measure / Inspect, Compute | Some filters report data; others compute quality/statistics. |
| `filter_texture` | Texture, Transfer / Projection, Convert | Should use uniform texture selector parameters everywhere. |
| `filter_colorproc` | Color / Visualization, Compute | Needs audit to distinguish color baking from quality computation. |
| `filter_color_projection` | Transfer / Projection, Raster | Current Raster grouping is reasonable but maybe less discoverable. |
| `filter_sampling` | Sampling, Transfer | Sampling creates new meshes or point sets from existing data. |
| `filter_voronoi` | Sampling, Generate, Remesh | Includes sampling and solid wireframe generation. |
| `filter_mls` | Compute, Reconstruct | Projection filters modify geometry; marching cubes creates new meshes. |
| `filter_geodesic` | Compute | Geodesic distances should behave as quality computation plus visualization hint. |
| `filter_xatlas` | Parametrization / UV | Should expose atlas quality/fragmentation terminology clearly. |
| `filter_parametrization` | Parametrization / UV | Use the clearest UV/parametrization vocabulary, even if it differs from MeshLab. |
| `filter_screened_poisson` | Reconstruct | Plugin provenance can remain implementation detail. |
| `filter_cgal` | Reconstruct, Geometry | Heavy dependency plugin, but menu should be user-task based. |
| `filter_mesh_booleans` | Geometry / Boolean | May deserve a `Boolean` subfamily. |
| `filter_layer` | Layer / Document, Camera / Render | Some actions are document commands exposed as filters for scripting/undo. |
| `filter_camera` | Camera / Render | Should clarify view-state vs document-state side effects. |
| `filter_icp` | Align / Registration | May deserve its own `Registration` family. |
| `filter_unsharp` | Enhance / Smooth | Could be under Smoothing or Geometry Enhancement. |
| `filter_trioptimize` | Remesh / Optimize | Local triangle optimization. |
| `filter_texture_defragmentation` | Texture | Specialized but fits Texture. |
| `filter_img_patch_param` | Parametrization / UV, Texture, Raster | Builds image/patch parametrization data from registered rasters; should be discoverable from both UV and raster projection workflows. |
| `filter_plymc` | Reconstruct | Marching-cubes-specific reconstruction plugin; user-facing names should emphasize generated surfaces rather than file/plugin provenance. |

## Open Questions

- Should `Registration` be a top-level family, or live under `Transform /
  Align`?
- Should `Sampling` remain top-level, or is it a subfamily of `Create /
  Generate`?
- Should `Reconstruction` be top-level, or should reconstruction filters stay
  under `Create / Generate`?
- Should view-only actions be filters, commands, or both?
- Are there any old names that must remain as temporary hidden aliases for a
  specific release-management reason?
- Should generated Python docs show only canonical names by default?
- How aggressive should we be in splitting the large `filter_meshing` family?
