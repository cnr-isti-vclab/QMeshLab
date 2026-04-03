# Architecture

QMeshLab follows a **Single Document Interface (SDI)** pattern where one `Document` is the central data model and multiple views observe it through Qt signals.

## Core Classes

### Document
- Owns an ordered list of `MeshEntry` (name, visibility flag, `VCGMesh`).
- Provides `loadMesh()` / `removeMesh()` to mutate the collection.
- Emits `meshAdded(int)` and `meshRemoved(int)` so views stay in sync.

### VCGMesh
- Defined in `vcgmesh.h` as a specialization of `vcg::tri::TriMesh` with standard per-vertex and per-face components (coords, normals, colors, quality, adjacency).

### MainWindow
- Creates and owns the `Document`.
- Sets up the central 3D view and dockable panels.
- Handles file menu actions, delegating I/O to the `Document`.

## Views

| View | Widget | Role |
|------|--------|------|
| 3D viewport | `RenderWidget` (QRhiWidget, central) | Renders meshes using Qt RHI |
| Mesh list | `MeshTreeWidget` (QTreeWidget, left dock) | Shows mesh names, vertex/face counts |

## Adding a New View

1. Create a widget that takes a `Document*` in its constructor.
2. Connect to `Document::meshAdded` / `meshRemoved` signals.
3. Instantiate it in `MainWindow` and add it as a dock widget (or tab).

## Data Flow

```
User action → MainWindow → Document (mutates data, emits signal)
                                ↓
                     ┌──────────┴──────────┐
                     │                     │
               RenderWidget          MeshTreeWidget
              (3D rendering)         (mesh info tree)
```
