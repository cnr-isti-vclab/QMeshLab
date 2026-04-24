# Screened Poisson Upstream Migration Notes

This directory hosts the vendored upstream `PoissonRecon` source tree used for the
ongoing migration of the QMeshLab Screened Poisson plugin.

Current state:

- The default runtime backend is now the vendored upstream `PoissonRecon` path.
- The legacy MeshLab-integrated implementation in `plugins/filter_screened_poisson/Src`
  is still kept as a fallback/reference implementation.
- The upstream code is vendored in `plugins/filter_screened_poisson/upstream/Src`
  and is adapted to QMeshLab through:
  - `plugins/filter_screened_poisson/upstream_backend.h/.cpp`
  - `plugins/filter_screened_poisson/upstream_qmeshlab_adapter.h/.cpp`

Why this separate subtree exists:

- The original MeshLab port is centered on `_Execute(...)`, `Octree`, and
  `MultiGridOctreeData`.
- The current upstream `PoissonRecon` is organized around `FEMTree`,
  `Reconstructors`, and stream adapters.
- Keeping the two implementations side-by-side lets us preserve a stable legacy
  fallback while we build and validate the new adapter.

Key upstream entry points:

- `PoissonRecon.cpp`
- `Reconstructors.h`
- `Reconstructors.streams.h`
- `FEMTree.h`
- `DataStream.h`
- `MultiThreading.h`

Additional maintenance notes:

- Provenance and local vendored-file changes are tracked in `UPSTREAM_PROVENANCE.md`.
- The current vendored upstream subtree differs from `.reference/PoissonRecon/Src`
  only in `Src/MultiThreading.h`.
- Runtime integration code lives outside `upstream/Src` on purpose so future upstream
  refreshes stay easy to review.

Planned phases:

1. Vendor and scaffold the upstream subtree
   - Status: completed
2. Build a QMeshLab-oriented input/output stream adapter
   - Status: completed
   - Implemented in `upstream_qmeshlab_adapter.h/.cpp`
   - Provides:
     - selected-mesh helpers
     - transformed oriented point streams over `Document`
     - optional per-vertex color stream
     - vector-backed output vertex and face streams
     - helpers for appending extracted geometry back into `VCGMesh`
3. Reproduce single-mesh reconstruction
   - Status: completed
4. Reproduce multi-visible-layer reconstruction
   - Status: completed
5. Restore quality-as-confidence, density output, and color transfer
   - Status: completed for the current backend path
6. Compare legacy/upstream outputs and switch the default backend
   - Status: completed
7. Improve maintainability of the vendored subtree
   - Status: completed
   - Provenance/update workflow documented in `UPSTREAM_PROVENANCE.md`
8. Remaining work
   - Improve cancellation granularity inside the upstream solver path
   - Continue parity testing on representative datasets
   - Reduce or remove the remaining legacy fallback once confidence is high
