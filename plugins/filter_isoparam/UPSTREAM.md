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

Two, both marked `QMeshLab:` in the source.

**The atlas is one UV space.** `AssociateDiamond` parks the diamond index in `WT(0).N()` as
scratch, and `SetWedgeCoords` never cleared it, so it escaped as the wedge's texture id --
291 distinct ids on a 1,200-vertex sphere, which makes every diamond look like a separate
texture to anything that reads `N()`. It is now zeroed once all three wedges of a face are
placed, and only then, because `QuadCoord` reads the index back out of it.

**`PrepareDiamonds` split out of `SetCoordinates`.** The loop that splits faces until each
one lies inside a single diamond, and the assignment that names that diamond in `WT(0).N()`,
are the only part of the layout QMeshLab reuses: `plugins/filter_isoparam/atlaslayout.h`
takes it from there and does its own chart building and packing. `SetCoordinates` still
exists and still lays the diamonds out on the square grid; nothing here calls it.

## Saving and loading the domain: not offered## Saving and loading the domain: not offered

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
