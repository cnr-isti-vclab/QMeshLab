# Isoparametrization integration

## Provenance

`upstream/` is MeshLab's `src/meshlabplugins/filter_isoparametrization`, the reference
implementation of Pietroni, Tarini and Cignoni, *Almost Isometric Mesh Parameterization
through Abstract Domains* (IEEE TVCG 2010). MeshLab is GPL-3.0-or-later, as is this
copy. Twenty headers, about 12,000 lines; MeshLab's own `filter_isoparametrization.{h,cpp}`
are **not** vendored, because they are the MeshLab plugin shell that
`isoparamfilterplugin.cpp` replaces.

The algorithm is templated on the mesh type, so it runs on `VCGMesh` unchanged --
`CMeshO` appeared only in the shell we did not take.

## The one patch we carry

MeshLab builds this against **levmar** (GPL-2.0), downloaded at configure time. It is used
at exactly two sites, both tiny derivative-free least-squares problems:

| Site | Problem |
|---|---|
| `opt_patch.h` | 2 parameters, 2 residuals (`slevmar_dif`) |
| `param_collapse.h` | 3 parameters, 4 residuals (`dlevmar_dif`) |

Both now minimise the sum of squares of the same residual functions through
**newuoa**, which is already vendored with vcglib (`wrap/newuoa/include/newuoa.h`) and is
likewise derivative-free. That removes the levmar download and its build system entirely.
Each wrapper maps the "folded/unusable" sentinel -- the residual functions return
`FLT_MAX`/`DBL_MAX` there, which squares to infinity -- onto one large finite penalty.

Verified on a closed sphere: the resulting parametrization has a one-way stretch
efficiency of 1.05, i.e. near-isometric, which is what the method is for.

## Updating

Re-copy the headers from a reviewed MeshLab commit, then re-apply the two newuoa
substitutions; search the diff for `slevmar_dif` and `dlevmar_dif` to find them. Nothing
else in the tree is modified.

## Console output

The reference code narrates its progress with about 120 `printf`/`fprintf` calls -- 109
lines for a 1,200-vertex mesh, 285 for a 40,000-vertex one. Rather than patch every site,
`isoparamfilterplugin.cpp` redefines `printf` and `fprintf` before including the headers,
so the whole narration is captured and written to the document log at Debug level. The
vendored sources are untouched by this.

## Patches to diam_parametrization.h

Three, all marked `QMeshLab:` in the source.

**The atlas is one UV space.** `AssociateDiamond` parks the diamond index in `WT(0).N()` as
scratch, and `SetWedgeCoords` never cleared it, so it escaped as the wedge's texture id --
291 distinct ids on a 1,200-vertex sphere, which makes every diamond look like a separate
texture to anything that reads `N()`. It is now zeroed once all three wedges of a face are
placed, and only then, because `QuadCoord` reads the index back out of it.

**`std::unary_function` dropped.** `SplitMidPoint` derived from it, which contributed only
the `argument_type`/`result_type` typedefs -- read by nothing in this tree and nothing in
vcglib's `RefineE`. C++17 removed it from the standard, and every standard library gates it
behind a different opt-in macro (`_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION` on
libc++, `_HAS_AUTO_PTR_ETC` on the MSVC STL), so the target used to carry a macro that only
worked on one of them and the Windows build broke on the other. Removing the base class is
portable and costs nothing.

**`PrepareDiamonds` split out of `SetCoordinates`.** The loop that splits faces until each
one lies inside a single diamond, and the assignment that names that diamond in `WT(0).N()`,
are the only part of the layout QMeshLab reuses: `plugins/filter_isoparam/atlaslayout.h`
takes it from there and does its own chart building and packing. `SetCoordinates` still
exists and still lays the diamonds out on the square grid; nothing here calls it.

### The geometry the layout rests on

Pietroni, Tarini and Cignoni, *Almost isometric mesh parameterization through abstract
domains*, IEEE TVCG 2010 -- section 4.1 in particular -- is the reference for all of this,
and worth reading before touching `atlaslayout.h`. The code's names are abbreviations of the
paper's, which is not obvious from the source:

- The domain is a set of **unit-sided equilateral triangles** with explicit adjacency and no
  3D embedding. That is why `ParametrizeDiamondEquilateral` is called with `edge_len = 1`.
- `getHDiamIndex` is *half*-diamond, not diamond. A **half-diamond domain**, one per domain
  edge, is the rhombus whose long diagonal is that edge and whose short diagonal joins the
  barycentres of the two triangles sharing it. With unit triangles its area is `sqrt(3)/6`.
  This -- not the two whole triangles -- is what `GE1Quad` flattens onto the unit square,
  which is easy to misread: the constants it is written with (`c1 = (sqrt(3)/6, 0)`) name the
  triangle *barycentres*, while the apexes are out at `sqrt(3)/2`. Anything unfolding a
  half-diamond has to come back out at `sqrt(3)/6` or it will be packed at a different texel
  density from its neighbours.
- `getHStarIndex` is likewise *half*-star: the k-agon joining the barycentres of the k
  triangles around a domain vertex.
- Half-diamonds tile the domain (one per edge), and so do half-stars (one per vertex) and
  face domains (one per triangle). They are three alternative partitions, not a common
  refinement. `DiamondParametrizator` only implements the half-diamond one.

The paper makes `g_E` **area-preserving** by choice -- section 4.1 says a star domain is
"rescaled so that its total area matches the area of k equilateral unit-sided triangles" --
which is what lets every chart be laid out at one texel density. `ParametrizeStarEquilateral`
hard-codes circumradius one instead, and a regular k-agon of circumradius one only has that
area at k = 6, so `GE0` returns a valence-five star 10% too large and a valence-seven star
10% too small. `atlaslayout.h` puts the rescale back in `starAreaCorrection`; nothing else
in the tree does, so anything else reading `GE0` on an irregular star inherits the error.

Section 6.0.1 is where the atlased-mesh filter comes from: it samples each half-diamond on a
grid into a square patch and packs the patches (their figure 7). QMeshLab keeps the square as
one chart shape, adds the rhombus -- which is the same patch with its samples in the place
section 6.0.1 says they belong, two quasi-equilateral triangles across the fixed diagonal --
and adds two ways of merging that are not in the paper: the hexagon, three half-diamonds
around a domain triangle, and the star, the half-diamonds around a domain vertex.

The fifth shape, `polygon`, is the paper's own half-star partition and needs no merging at
all. It rides on the half-diamond split rather than needing one of its own: a face inside the
half-diamond of edge (v,w) lies inside both triangles sharing that edge, and both are in v's
star and in w's star, so whichever half-star the face's centre falls in can unfold it. All it
takes is `Phi` on the face centre, `getHStarIndex`, and `GE0` -- no contention, no leftovers,
and the lowest area distortion of the five, since `GE0` is area-preserving where the
half-diamond-to-square map of section 6.0.1 explicitly is not.

## Saving and loading the domain: not offered## Saving and loading the domain: not offered## Saving and loading the domain: not offered

`IsoParametrization` has `SaveBaseDomain` / `LoadBaseDomain`, and building the domain is
by far the most expensive step of the family, so a save/reload pair looks like the obvious
convenience. It does not work, and MeshLab does not offer it either -- both call sites are
commented out in its own `filter_isoparametrization.cpp`.

Investigated 2026-09-06. Saving does write a valid-looking file once its `fprintf` calls
are allowed through to the file (see the console-output note above -- capturing them
produced a zero-byte file). Loading it back then aborts inside
`param_domain::getClosest`, on `assert(index < HresDomain->fn)`, as soon as any consumer
touches the domain: the file records the abstract mesh and a per-vertex mapping, but not
the per-domain `HresDomain` / `ordered_faces` structures the consumers index into.
`LoadBaseDomain` ends in `Update()` where the compute path ends in `Init()`, and calling
`Init()` afterwards does not rebuild them either.

Reviving this means reconstructing that state after a load, in code the original authors
left disabled. Worth doing only if recomputation time becomes a real complaint.
