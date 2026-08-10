# External dependencies

## JKQtPlotter

QMeshLab uses only JKQtPlotter's `JKQTCommon` and `JKQTMathText` libraries to
render formulas in filter help. It is kept as a pinned Git submodule instead of
the `jkqtplotter` vcpkg package because that package:

- depends on vcpkg's Qt, while QMeshLab intentionally uses an external Qt
  installation;
- builds the plotting libraries and additional dependencies that QMeshLab does
  not need;
- does not provide a feature for installing only the MathText component.

The root CMake configuration disables the unused JKQtPlotter components and
builds only the formula renderer with the embedded Fira Math font.

Initialize the dependency after cloning QMeshLab with:

```sh
git submodule update --init external/jkqtplotter
```

## MeshFix

QMeshLab builds the reusable core of MeshFix directly from the pinned
`external/meshfix` submodule. The upstream tree stays unmodified; the adapter,
provenance, update procedure, and known API limitations are documented in
[`plugins/filter_meshfix/UPSTREAM.md`](../plugins/filter_meshfix/UPSTREAM.md).

Initialize it after cloning with:

```sh
git submodule update --init external/meshfix
```

## QSlim

QMeshLab preserves Garland's original QSlim edge-collapse implementation in the
pinned `external/qslim` submodule. Only its reusable MixKit core is compiled;
integration, licensing, and update details are in
[`plugins/filter_qslim/UPSTREAM.md`](../plugins/filter_qslim/UPSTREAM.md).

Initialize it after cloning with:

```sh
git submodule update --init external/qslim
```

## QuadWild-BiMDF

QMeshLab keeps the GPL-3.0-or-later QuadWild-BiMDF command-line pipeline in a pinned,
recursive submodule. Its two required executables are built in an isolated
CMake project and bundled with QMeshLab; no upstream types or targets enter the
application. Integration details and the update procedure are documented in
[`plugins/filter_quadwild/UPSTREAM.md`](../plugins/filter_quadwild/UPSTREAM.md).

Initialize it after cloning with:

```sh
git submodule update --init --recursive external/quadwild-bimdf
```
