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
