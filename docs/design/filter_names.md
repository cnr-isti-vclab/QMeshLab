# Filter Name Proposal

Proposed display names aligned to the naming grammar in [Vocabulary](vocabulary.md) §6:

```text
Verb Object [(Backend)]
```

**Status: the 30 `Meshing` display names are APPLIED** (2026-07-30). The other roots
follow the same treatment; `Attribute` (68) and `Creation` (38) are the largest.

Python names in the tables below are **not** applied — they are pass 2, being the
scripting contract.

Python names are shown too (`verb_object[_backend]`) because they should change in the
same breath, but they are **pass 2** — they are the scripting contract, so they land
separately from the display names.

## Rules being applied

1. **Lead with a canonical verb** — 61 % of current names do not.
2. **Never repeat the category** — the menu already says `Meshing/Simplification`, so
   `Simplification: Clustering Decimation` says it twice.
3. **Backend in parentheses only to disambiguate competing implementations** — required
   by the algorithm-archive model, not decoration.
4. **Title Case, no irregular capitals** — `Delete ALL Faces` → `Remove All Faces`.
5. **`Remove`, not `Delete`** — a settled ruling in the verb lexicon.

## Meshing/Boolean

Currently every name begins `Mesh Boolean:`, repeating the category. The result of a
boolean *is* a conventionally named object, so these are named for the result rather than
the action — the noun-phrase exception recorded in [Vocabulary](vocabulary.md) §6.

| Current | Proposed | Python |
|---|---|---|
| Mesh Boolean: Union | **Mesh Union** | `mesh_union` |
| Mesh Boolean: Intersection | **Mesh Intersection** | `mesh_intersection` |
| Mesh Boolean: Difference | **Mesh Difference** | `mesh_difference` |
| Mesh Boolean: Symmetric Difference (XOR) | **Mesh Symmetric Difference** | `mesh_symmetric_difference` |

Keeping the nouns also means **no new boolean verbs are needed** — an earlier draft would
have added `Unite`/`Intersect`/`Subtract`/`Exclude` to the lexicon for these four filters
alone.

## Meshing/Deletion

Pure application of the `Remove`-not-`Delete` ruling, plus the casing rule.

| Current | Proposed | Python |
|---|---|---|
| Delete ALL Faces | **Remove All Faces** | `remove_all_faces` |
| Delete Selected Faces | **Remove Selected Faces** | `remove_selected_faces` |
| Delete Selected Vertices | **Remove Selected Vertices** | `remove_selected_vertices` |
| Delete Selected Faces and Vertices | **Remove Selected Faces and Vertices** | `remove_selected_faces_and_vertices` |

## Meshing/Remeshing

| Current | Proposed | Python |
|---|---|---|
| Remeshing: Isotropic Explicit Remeshing | **Remesh Isotropically** | `remesh_isotropically` |
| Uniform Mesh Resampling | **Remesh Uniformly by Volumetric Resampling** | `remesh_uniformly_by_volumetric_resampling` |
| Curvature flipping optimization | **Flip Edges by Curvature** | `flip_edges_by_curvature` |
| Planar flipping optimization | **Flip Edges by Planarity** | `flip_edges_by_planarity` |
| Cut mesh along crease edges | **Cut Along Crease Edges** | `cut_along_crease_edges` |

`Remesh Isotropically` drops "Explicit": there is no implicit variant to distinguish it
from, so the word only added length. It stays in the description.

*Remesh Uniformly by Volumetric Resampling* names the method because the filter really is
volumetric — it samples a signed distance field on a regular grid and reconstructs with
marching cubes. An earlier draft read *Resample Uniformly*, which broke the verb lexicon:
`Resample` is a **rejected** synonym for `Remesh` on surfaces. The canonical verb leads,
and "resampling" survives only as the method.

Despite the marching cubes, it stays `Meshing/Remeshing` rather than
`Creation/Reconstruction`: the discriminator is the *input*, and here the input is an
existing valid mesh, not unstructured data from which a surface must be inferred.

## Meshing/Quad

Moved out of `Remeshing` into their own subcategory (**applied**): converting between
triangle and quad representation is a distinct concern from improving element shape, and
this set is expected to grow.

| Current | Proposed | Python |
|---|---|---|
| Tri to Quad by 4-8 Subdivision | **Convert to Quads by 4-8 Subdivision** | `convert_to_quads_by_4_8_subdivision` |
| Tri to Quad by smart triangle pairing | **Convert to Quads by Triangle Pairing** | `convert_to_quads_by_triangle_pairing` |
| Turn into Quad-Dominant mesh | **Convert to Quad-Dominant Mesh** | `convert_to_quad_dominant_mesh` |
| Turn into a Pure-Triangular mesh | **Convert to Pure Triangles** | `convert_to_pure_triangles` |

## Meshing/Simplification

This is where the archive model bites: **three** quadric-edge-collapse implementations
coexist, so the backend suffix is load-bearing rather than decorative.

| Current | Proposed | Python |
|---|---|---|
| Simplification: Quadric Edge Collapse Decimation | **Simplify by Quadric Edge Collapse** | `simplify_by_quadric_edge_collapse` |
| Simplification: Original QSlim Quadric Edge Collapse | **Simplify by Quadric Edge Collapse (QSlim)** | `simplify_by_quadric_edge_collapse_qslim` |
| Simplification: Quadric Edge Collapse Decimation (with texture) | **Simplify by Quadric Edge Collapse with Texture** | `simplify_by_quadric_edge_collapse_with_texture` |
| Simplification: Clustering Decimation | **Simplify by Vertex Clustering** | `simplify_by_vertex_clustering` |
| Simplification: Edge Collapse for Marching Cube meshes | **Simplify Marching-Cubes Mesh by Edge Collapse** | `simplify_marching_cubes_mesh_by_edge_collapse` |
| Point Cloud Simplification | **Simplify Point Cloud** | `simplify_point_cloud` |

"Decimation" disappears throughout — it is a rejected synonym for `Simplify`, and
`Simplification: … Decimation` said the same thing twice. "Original" is dropped from the
QSlim entry: the parenthesised backend already carries that information.

## Meshing/Subdivision

| Current | Proposed | Python |
|---|---|---|
| Subdivision Surfaces: Loop | **Subdivide by Loop** | `subdivide_by_loop` |
| Subdivision Surfaces: LS3 Loop | **Subdivide by LS3 Loop** | `subdivide_by_ls3_loop` |
| Subdivision Surfaces: Butterfly Subdivision | **Subdivide by Butterfly** | `subdivide_by_butterfly` |
| Subdivision Surfaces: Midpoint | **Subdivide by Midpoint** | `subdivide_by_midpoint` |
| Subdivision Surfaces: Catmull-Clark | **Subdivide by Catmull-Clark** | `subdivide_by_catmull_clark` |
| Subdivision Surfaces: Doo Sabin | **Subdivide by Doo-Sabin** | `subdivide_by_doo_sabin` |
| Refine User-Defined | **Refine by User Expression** | `refine_by_user_expression` |

The set becomes perfectly regular: `Subdivide by <Scheme>`. *Doo Sabin* gains its
conventional hyphen. *Refine* is kept for the one genuinely adaptive, predicate-driven
case, which is exactly the exception the verb lexicon allows.

## Verbs added to the lexicon

Two, both now recorded in [Vocabulary](vocabulary.md) §3:

| Verb | Means | Why the existing lexicon was not enough |
|---|---|---|
| `Flip` | Reconnect by flipping edges; connectivity changes, vertices do not move | A precise, well-known mesh operation; `Remesh` is too coarse for it |
| `Cut` | Split a surface along a curve, introducing a boundary | `Split` is claimed by layer operations, so reusing it here would be ambiguous |

## Rulings

1. ~~Should the triangle↔quad conversions have their own subcategory?~~ **Yes —
   `Meshing/Quad`, applied.** The ontology and the four descriptors are updated; the set
   is expected to grow.
2. ~~Is `Simplify by Quadric Edge Collapse with Texture` too long?~~ **Kept as proposed.**
3. ~~Verb or noun for the booleans?~~ **Noun**: `Mesh Union`, `Mesh Intersection`,
   `Mesh Difference`, `Mesh Symmetric Difference`. Recorded in the vocabulary as the
   named-result exception so they are not "corrected" back to verb-first.

Applied: all 30 display names. Verified — no old name survives as a filter name, no
duplicate display names across the 272, JSON intact, build and tests clean.

Not applied: the 30 Python names (pass 2), and the remaining 242 filters, where
`Attribute` (68) and `Creation` (38) are the largest and where the *Colorize vs Compute*
confusion lives.

Note for whoever regenerates the docs: `docs/api/filters.rst` still contains the old
names, but it is a **stale artifact** — `generate_api.py` deletes it, so it disappears on
the next `--generate-docs` run rather than needing a hand edit.
