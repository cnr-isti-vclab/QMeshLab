# Architecture

QMeshLab follows a **Single Document Interface (SDI)** pattern where one `Document` is the central data model and multiple views observe it through Qt signals.

## Core Classes

### Document
- Owns an ordered list of `MeshEntry` (name, visibility flag, `VCGMesh`).
- Provides `loadMesh()` / `removeMesh()` to mutate the collection.
- Emits `meshAdded(int)` and `meshRemoved(int)` so views stay in sync.
- Owns a per-document log and emits log signals when new messages are appended.
- Owns a `MeshIOPluginManager` and delegates all file loading to it. `loadMesh()` only orchestrates: find plugin → call load → post-process (bounds, normals) → add entry → emit signals.

Loading a mesh is managed in an isolated way via a plugin mechanism that can be extended in the future to support more formats. The plugin should be able to load a mesh and return a VCGMesh, and the Document should be able to add it to its collection and emit the appropriate signals.

## Plugin System

### MeshIOPlugin (`meshioplugin.h`)
Pure abstract interface every I/O plugin must implement:
- `canLoad(filename)` — extension-based format detection
- `load(filename, mesh, cb)` — fills a `VCGMesh`, forwards a `vcg::CallBackPos*` for progress
- `filterString()` — Qt file dialog filter, e.g. `"Mesh Files (*.ply *.obj)"`
- `errorString(errCode)` — human-readable error message

### MeshIOPluginManager (`meshiopluginmanager.h`)
Registry holding registered plugins in order. `pluginFor(filename)` returns the first matching plugin. `openDialogFilter()` builds the combined Qt file dialog filter from all plugins plus "All Files (*)".

### VCGImportPlugin (`vcgimportplugin.h`)
Built-in plugin wrapping `vcg::tri::io::Importer<VCGMesh>`. Supports PLY, OBJ, STL, OFF.

### Adding a New Format
1. Create a class inheriting `MeshIOPlugin` and implement the four methods.
2. Register it with `m_pluginManager->registerPlugin(std::make_unique<MyPlugin>())` in `Document::Document()`.

Plugins are owned by the application. The plugins are responsible for loading the meshes and returning a VCGMesh, and the Document is responsible for adding it to its collection and emitting the appropriate signals. The plugins can be implemented as shared libraries that are loaded at runtime. The plugins should be able to handle different file formats and provide a consistent interface for loading meshes.

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
| Layers | `LayerWidget` (QTreeWidget, right dock) | Shows mesh names, vertex/face counts |
| Log | `QPlainTextEdit` (bottom dock) | Shows document log messages and vcglib import progress |

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
               RenderWidget            LayerWidget
              (3D rendering)         (mesh info tree)
```
