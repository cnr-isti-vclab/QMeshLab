# Filter Name Proposal

Proposed display names aligned to the naming grammar in [Vocabulary](vocabulary.md) §6:

```text
Verb Object [(Backend)]
```

**Status: display names APPLIED for `Meshing`** (30, 2026-07-30) **and `Attribute`**
(64, 2026-08-04). Remaining roots, largest first: `Creation` (38), `Geometry` (33),
`Selection` (26), `Document` (22), `Repair` (19), `Parametrization` (15),
`Measurement` (13), `Transfer` (13), `Texture` (3).

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

Note for whoever regenerates the docs: `docs/api/filters.rst` still contains the old
names, but it is a **stale artifact** — `generate_api.py` deletes it, so it disappears on
the next `--generate-docs` run rather than needing a hand edit.

---

# Round 2 — `Attribute` (65 filters → 64)

**Status: APPLIED** (2026-08-04). 56 names changed, 8 already conformed, 1 filter
removed as a duplicate. Verified mechanically: JSON valid across all 31 descriptor
files, no old name survives as a filter name, no duplicate display name among the 277,
every proposed name present, build clean, test results unchanged from the pre-change
baseline (`FilterTests` 22/1, `FilterCreationTests` 19/0, `DocumentTests` 15/6 — the
failures are pre-existing and were confirmed identical with the changes stashed).

The largest root and the least regular: 61 of the 65 names do not lead with a verb, and
the same operation is spelled three different ways across plugins (`Re-Compute`,
`Compute`, bare noun). It is also where the `scalar`-not-`quality` ruling
([Vocabulary](vocabulary.md) §4) first bites — 17 names say *Quality* today.

Three cross-cutting patterns do most of the work:

| Pattern | Applies to | Example |
|---|---|---|
| `Compute <Object> by Expression` | the 8 `filter_expression` entries | `Per Vertex Color Function` → **Compute Vertex Color by Expression** |
| `Sharpen <Object> by Unsharp Mask` | the 3 unsharp entries here (a 4th is in `Geometry`) | `UnSharp Mask Color` → **Sharpen Vertex Color by Unsharp Mask** |
| `Compute <Object> (<Backend>)` | competing curvature / geodesic implementations | `Discrete Curvatures` → **Compute Curvature (Discrete)** |

## Attribute/Normal (13)

| Current | Proposed | Python |
|---|---|---|
| Re-Compute Vertex Normals | **Compute Vertex Normals** | `compute_vertex_normals` |
| Re-Compute Face Normals | **Compute Face Normals** | `compute_face_normals` |
| Re-Compute Per-Polygon Face Normals | **Compute Polygon Face Normals** | `compute_polygon_face_normals` |
| Compute normals for point sets | **Compute Point Cloud Normals** | `compute_point_cloud_normals` |
| Per Vertex Normal Function | **Compute Vertex Normals by Expression** | `compute_vertex_normals_by_expression` |
| Per Face Normal Function | **Compute Face Normals by Expression** | `compute_face_normals_by_expression` |
| Normalize Vertex Normals | **Normalize Vertex Normals** | `normalize_vertex_normals` |
| Normalize Face Normals | **Normalize Face Normals** | `normalize_face_normals` |
| Re-Orient vertex normals using cameras | **Orient Vertex Normals by Cameras** | `orient_vertex_normals_by_cameras` |
| Reorient Face Normals by Geometry | **Orient Face Normals by Ray Casting** | `orient_face_normals_by_ray_casting` |
| Smooth Face Normals | **Smooth Face Normals** | `smooth_face_normals` |
| Smooth normals on point sets | **Smooth Point Cloud Normals** | `smooth_point_cloud_normals` |
| UnSharp Mask Normals | **Sharpen Face Normals by Unsharp Mask** | `sharpen_face_normals_by_unsharp_mask` |

`Re-Compute` → `Compute` is the casing rule in [Vocabulary](vocabulary.md) §5 applied
literally; the "Re-" carried no information, since every one of these overwrites whatever
was there.

*Orient Face Normals by Ray Casting* keeps its method in the name because it competes
with `Repair/Topology`'s coherent-orientation filter, which works purely topologically.
The two are now visibly a pair rather than two unrelated names.

## Attribute/Scalar (19)

The `Quality` → `Scalar` root-and-branch pass. Note that in the geodesic family the word
disappears entirely rather than being replaced: the object of the sentence is the
*distance*, and the category already says the result is a scalar.

| Current | Proposed | Python |
|---|---|---|
| Compute Border Distance Quality | **Compute Geodesic Distance from Border** | `compute_geodesic_distance_from_border` |
| Compute Geodesic Distance Quality from Point | **Compute Geodesic Distance from Point** | `compute_geodesic_distance_from_point` |
| Compute Geodesic Distance Quality from Selection | **Compute Geodesic Distance from Selection** | `compute_geodesic_distance_from_selection` |
| Compute Heat Geodesic Distance Quality from Selection | **Compute Geodesic Distance from Selection (Heat Method)** | `compute_geodesic_distance_from_selection_heat` |
| Compute Ambient Occlusion | **Compute Ambient Occlusion** | `compute_ambient_occlusion` |
| Compute Obscurance | **Compute Obscurance** | `compute_obscurance` |
| Compute Shape Diameter Function | **Compute Shape Diameter Function** | `compute_shape_diameter_function` |
| Generate Scalar Harmonic Field | **Compute Harmonic Scalar Field** | `compute_harmonic_scalar_field` |
| Per Face Geometric Quality | **Compute Face Scalar from Geometry** | `compute_face_scalar_from_geometry` |
| Per Face Texture Distortion | **Compute UV Distortion** | `compute_uv_distortion` |
| Quality from raster coverage (Vertex) | **Compute Vertex Scalar from Raster Coverage** | `compute_vertex_scalar_from_raster_coverage` |
| Quality from raster coverage (Face) | **Compute Face Scalar from Raster Coverage** | `compute_face_scalar_from_raster_coverage` |
| Vertex Quality from Camera | **Compute Vertex Scalar from Camera** | `compute_vertex_scalar_from_camera` |
| Per Vertex Quality Function | **Compute Vertex Scalar by Expression** | `compute_vertex_scalar_by_expression` |
| Per Face Quality Function | **Compute Face Scalar by Expression** | `compute_face_scalar_by_expression` |
| Clamp Vertex Quality | **Clamp Vertex Scalar** | `clamp_vertex_scalar` |
| Saturate Vertex Quality | **Clamp Vertex Scalar Gradient** | `clamp_vertex_scalar_gradient` |
| Smooth Vertex Quality | **Smooth Vertex Scalar** | `smooth_vertex_scalar` |
| UnSharp Mask Quality | **Sharpen Vertex Scalar by Unsharp Mask** | `sharpen_vertex_scalar_by_unsharp_mask` |

*Generate* → *Compute*: `Generate` is a rejected synonym for `Create`, and `Create` means
*produce a new layer*. This filter writes an attribute on the current mesh, so the verb
is `Compute`.

*Saturate Vertex Quality* is the one outright translation: nothing about it saturates
anything: it bounds the spatial gradient of the field so values cannot change faster than
a given rate. **Clamp Vertex Scalar Gradient** says what it does, and pairs it with the
filter directly above:

| | Bounds |
|---|---|
| `Clamp Vertex Scalar` | the values |
| `Clamp Vertex Scalar Gradient` | the rate of change of the values |

Caveat recorded so the name is not "corrected" later: the implementation is one-sided,
only ever decreasing values, so it is not a symmetric two-sided clamp. It does enforce
|∇q| ≤ threshold, which is a clamp of the gradient, and the description spells out the
conservative behaviour.

## Attribute/Curvature (4)

Three competing curvature estimators plus the directions filter. Backend in parentheses
is load-bearing here, exactly as in `Meshing/Simplification`.

| Current | Proposed | Python |
|---|---|---|
| Discrete Curvatures | **Compute Curvature (Discrete)** | `compute_curvature_discrete` |
| Compute APSS Curvature Quality | **Compute Curvature (APSS)** | `compute_curvature_apss` |
| Compute RIMLS Curvature Quality | **Compute Curvature (RIMLS)** | `compute_curvature_rimls` |
| Compute curvature principal directions | **Compute Principal Curvature Directions** | `compute_principal_curvature_directions` |

The three estimators become directly comparable in the menu, which is the point of the
archive model. Today they sort apart (`Compute APSS…`, `Compute RIMLS…`, `Discrete…`) and
read as unrelated filters.

## Attribute/Custom (4)

| Current | Proposed | Python |
|---|---|---|
| Define New Per Vertex Custom Scalar Attribute | **Define Custom Vertex Scalar Attribute** | `define_custom_vertex_scalar_attribute` |
| Define New Per Vertex Custom Point Attribute | **Define Custom Vertex Point Attribute** | `define_custom_vertex_point_attribute` |
| Define New Per Face Custom Scalar Attribute | **Define Custom Face Scalar Attribute** | `define_custom_face_scalar_attribute` |
| Define New Per Face Custom Point Attribute | **Define Custom Face Point Attribute** | `define_custom_face_point_attribute` |

Only `New` and `Per` are dropped, both redundant. `Define` is kept rather than converted
to the lexicon's `Set` — see ruling 2.

## Attribute/Color (24)

> **One filter removed, not renamed.** *Quality Mapper applier* was a duplicate of
> *Colorize by vertex Quality*: identical parameters, identical categories and
> input/output declarations, and literally the same code path (`kFilterMapVQuality ||
> kFilterQualityMapper` in one `if`). It survived as a MeshLab compatibility shim — in
> MeshLab it was driven by the Quality Mapper *dialog*, an editable transfer-function
> curve that QMeshLab never ported, so the filter was wired to the plain ramp and became
> a clone. Descriptor entry and dispatch branch deleted; no alias kept, since a
> consistent Python API for QMeshLab outranks pymeshlab script compatibility (ruling 5).
> A real transfer-function editor remains a legitimate future feature, and the name
> *Colorize Vertices by Scalar Transfer Function* is reserved for it.

The `Colorize` / `Set` split is the organising idea, and it resolves the *Colorize vs
Compute* confusion flagged at the end of round 1:

- **`Colorize`** — colour *derived from other data* (a scalar field, a distance, a noise
  field). Reading the mesh tells you something.
- **`Set`** — colour *assigned*, carrying no information about the geometry (a fixed
  colour, a random one per component).
- **`Adjust` / `Invert` / …** — colour *edited*, an image-processing operation on colour
  that is already there.

| Current | Proposed | Python |
|---|---|---|
| Colorize by vertex Quality | **Colorize Vertices by Scalar** | `colorize_vertices_by_scalar` |
| Colorize by face Quality | **Colorize Faces by Scalar** | `colorize_faces_by_scalar` |
| Perlin color | **Colorize Vertices by Perlin Noise** | `colorize_vertices_by_perlin_noise` |
| Disk Vertex Coloring | **Colorize Vertices by Disk Distance** | `colorize_vertices_by_disk_distance` |
| Voronoi Vertex Coloring | **Colorize Vertices by Voronoi Regions** | `colorize_vertices_by_voronoi_regions` |
| Per Vertex Color Function | **Compute Vertex Color by Expression** | `compute_vertex_color_by_expression` |
| Per Face Color Function | **Compute Face Color by Expression** | `compute_face_color_by_expression` |
| Vertex Color Filling | **Set Vertex Color** | `set_vertex_color` |
| Set Mesh Color | **Set Mesh Color** | `set_mesh_color` |
| Random Face Color | **Set Random Face Color** | `set_random_face_color` |
| Random Component Color | **Set Random Component Color** | `set_random_component_color` |
| PerMesh Color Scattering | **Set Random Layer Color** | `set_random_layer_color` |
| Vertex Color Brightness Contrast Gamma | **Adjust Vertex Color Brightness/Contrast/Gamma** | `adjust_vertex_color_brightness_contrast_gamma` |
| Vertex Color Levels Adjustment | **Adjust Vertex Color Levels** | `adjust_vertex_color_levels` |
| Vertex Color White Balance | **Adjust Vertex Color White Balance** | `adjust_vertex_color_white_balance` |
| Vertex Color Invert | **Invert Vertex Color** | `invert_vertex_color` |
| Vertex Color Desaturation | **Desaturate Vertex Color** | `desaturate_vertex_color` |
| Vertex Color Thresholding | **Threshold Vertex Color** | `threshold_vertex_color` |
| Vertex Color Colourisation | **Tint Vertex Color** | `tint_vertex_color` |
| Equalize Vertex Color | **Equalize Vertex Color** | `equalize_vertex_color` |
| Color noise | **Add Noise to Vertex Color** | `add_noise_to_vertex_color` |
| Smooth: Laplacian Vertex Color | **Smooth Vertex Color** | `smooth_vertex_color` |
| Smooth: Laplacian Face Color | **Smooth Face Color** | `smooth_face_color` |
| UnSharp Mask Color | **Sharpen Vertex Color by Unsharp Mask** | `sharpen_vertex_color_by_unsharp_mask` |

`PerMesh Color Scattering` → **Set Random Layer Color** applies the `mesh` vs `layer`
ruling: it iterates over document layers and gives each a distinct colour, so it is a
layer operation even though what it writes is a mesh attribute.

`Smooth: Laplacian …` drops the method: Laplacian is the only smoothing offered, and if a
second scheme ever lands it comes back as `Smooth Vertex Color (Taubin)`.

## Verbs added to the lexicon

Two new general verbs, plus a group of standard attribute-editing operations that are
better recorded as a set than as ten separate lexicon rows.

| Verb | Means | Why the existing lexicon was not enough |
|---|---|---|
| `Orient` | Make normals point consistently, or toward a reference | Distinct from `Compute` (values are flipped, not recalculated); also the canonical form of `Re-Orient`/`Reorient` |
| `Sharpen` | Enhance local variation in an attribute | The four unsharp-mask filters had no verb at all; `Sharpen X by Unsharp Mask` matches `Simplify by …` |

**Attribute-editing verbs** (standard image/signal operations, admitted as a closed
group): `Normalize`, `Adjust`, `Clamp`, `Invert`, `Equalize`, `Desaturate`, `Threshold`,
`Tint`. Each keeps its ordinary meaning; none may be used where `Compute` (derive from
geometry) or `Set` (assign a constant) is accurate.

`Define` is also admitted, but **narrowly**: declaring a new named custom attribute, and
nothing else. It is otherwise still a rejected synonym for `Set`.

## Duplicate sweep (2026-08-04)

Run before applying round 2, prompted by finding that *Quality Mapper applier* was a
clone. Five independent tests over all 277 filters:

| Test | What it looks for | Hits | True positives |
|---|---|---|---|
| A | Identical behavioural signature (parameters + domains + requirements + `outputModifies` + categories) | 12 groups | 0 |
| B | Two or more filter ids sharing one dispatch branch | 9 groups | 0 |
| C | Shared branch where the ids are **never re-tested inside** — the *Quality Mapper* signature | 1 | 0 |
| D | Cross-plugin name-token overlap ≥ 0.6 | 12 pairs | 0 (1 naming defect) |
| E | Duplicate `id`, `pythonName`, or display name | 0 | 0 |

**Result: no further clones. *Quality Mapper applier* was the only one.**

Test A is weak alone — filters with no parameters collide trivially (the five Platonic
solids, the four igl booleans, three no-parameter selections), as do families that differ
only in an internal constant (`Subdivide by Butterfly` / `by Midpoint`). Test C is the
sharp one, and its single hit is the APSS-vs-RIMLS surface factory in `filter_mls`, where
the three ids legitimately share "use APSS" and are discriminated at three later sites.

### Not duplicates, but defects found on the way

1. **`Create Selection Perimeter Polyline` has a wrong description.** It says the polyline
   is "composed by the selected edges of the current mesh"; the code walks *selected
   faces* and emits each edge whose neighbour is unselected. Its sibling
   `Build a Polyline from Selected Edges` is the one that reads the edge selection
   (`BuildFromFaceEdgeSel`). Two genuinely different filters whose descriptions claim the
   same input.
2. **Two indistinguishable names for two different algorithms** — the only real find of
   test D, and one to fix in the `Transfer` round rather than here:

   | Name | Plugin | Mechanism |
   |---|---|---|
   | *Transfer Color: Texture to Vertex* | `colorproc` | exact per-wedge UV lookup, same mesh only |
   | *Transfer: Texture to Vertex Color* | `texture` | closest-point resampling in world space, across two layers, with a distance bound |

   Neither is redundant: the first is the exact fast path, the second the only one that
   crosses layers. But the names differ by word order alone, which is unusable. The
   second needs its mechanism in the name.
3. **No filter can declare "requires a selection".** Both polyline filters check for a
   selection at runtime and fail with a message; the `inputRequirements` schema has keys
   for vertices, edges, faces, colour, texcoords, textures and quality, but none for
   selection state, so the UI cannot grey them out. A framework gap, noted not fixed.

## Rulings

All settled (2026-08-04).

1. ~~Are the slashes in `Adjust Vertex Color Brightness/Contrast/Gamma` acceptable?~~
   **Yes, kept.** The filter really does all three.
2. ~~`Define` or `Set` for custom attributes — or `Create`?~~ **`Define`, admitted
   narrowly.** `Create` was considered and rejected: it carries the invariant *a new
   layer appears in the layer list*, which holds exactly today — every filter named
   `Create *` declares `outputDomain: NewMeshes`, while all four custom-attribute
   filters are `ModifyCurrentMesh`. Spending `Create` here would break a rule that makes
   the menu readable, for the sake of four filters. `Set` was rejected because these
   declare a new named attribute rather than assign to an existing one.
3. ~~`Saturate` → `Limit … Gradient`?~~ **`Clamp Vertex Scalar Gradient`** — better than
   the proposal, since it reuses a lexicon verb instead of adding `Limit` and pairs the
   filter with `Clamp Vertex Scalar` directly above it.
4. ~~`Compute Curvature (Discrete)` demotes the familiar name into a parenthesis — worth
   the regularity?~~ **Yes, proceed.** Verified that the three are genuinely competing
   implementations of one quantity rather than different quantities sharing a word:

   | Filter | Method | Writes | Offers |
   |---|---|---|---|
   | Discrete Curvatures | discrete differential operators (Meyer et al.) | `VQ` | Mean / Gaussian / RMS / ABS |
   | Compute APSS Curvature Quality | algebraic point set surfaces (MLS) | `VQ` | Mean / Gauss / K1 / K2 / ApproxMean |
   | Compute RIMLS Curvature Quality | robust implicit MLS | `VQ` | Mean / Gauss / K1 / K2 |

   Same output slot, same quantity, overlapping type enums — exactly what the
   parenthesised backend exists for, and the same trade already accepted across
   `Meshing`. Search still finds "discrete" via the description.

   *Compute curvature principal directions* stays apart on merit: it is the only one
   writing `VA`, and its `Method` enum is five further algorithms (Taubin, PCA, Normal
   Cycles, Quadric Fitting, Scale-Dependent Quadric Fitting). Its unique output is the
   directions, so it is named for them.
5. ~~Keep MeshLab `pythonName`s as aliases when renaming?~~ **No.** A consistent Python
   API for QMeshLab outranks pymeshlab script compatibility. Pass 2 therefore renames all
   `pythonName`s outright, with no alias mechanism and no deprecation window. Recorded
   here because it removes a prerequisite that pass 2 would otherwise have needed.
