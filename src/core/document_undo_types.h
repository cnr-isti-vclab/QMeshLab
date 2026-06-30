#pragma once

#include "camerashot.h"
#include "meshioplugin.h"
#include "rasterplane.h"
#include "vcgmesh.h"
#include "viewstate.h"

#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// ---------------------------------------
// Layer kind
// ---------------------------------------

enum class CurrentLayerKind {
    None,
    Mesh,
    Raster
};

// ---------------------------------------
// Script action — reproducible action
// metadata for undo history.
// ---------------------------------------

struct ScriptAction {
    QString     kind;        // "filter", "load_mesh", "load_raster", "save_mesh", "load_project"
    QString     filterKey;   // fully qualified filter key (pluginId::filterId)
    QVariantMap params;      // filter parameters (empty for load/save)
    QStringList  filePaths;   // file paths for load/save operations
    QString pythonCall;      // exact Python call for filter actions, when available
    QString compactPythonCall;
    int currentMeshIndex = -1;
    int currentRasterIndex = -1;
    CurrentLayerKind currentLayerKind = CurrentLayerKind::None;
};

// ---------------------------------------
// Full undo snapshot types
// ---------------------------------------

// A lightweight snapshot of a single mesh, suitable for long-term undo storage.
// Cheap metadata fields are copied by value. Geometry (VCGMesh) is held behind a
// shared_ptr so that multiple checkpoints with the same (meshId, geometryRevision)
// can share a single copy — the common case when an action does not touch geometry
// (e.g. toggle visibility, change transform, rename).
struct MeshSnapshot {
    std::uint64_t meshId = 0;
    std::uint64_t geometryRevision = 0;
    std::uint64_t materialRevision = 0;
    QMatrix4x4 transform;
    QString name;
    QString sourcePath;
    QStringList textureFileNames;
    QStringList textureFilePaths;
    std::vector<MeshIOTextureAsset> textureAssets;
    MeshIOMaterialSet materialSet;
    bool visible = false;
    bool modified = false;
    int ioMask = 0;
    // Shared, immutable geometry; never null after capture.
    std::shared_ptr<const VCGMesh> geometry;
};

struct RasterSnapshot {
    std::uint64_t rasterId = 0;
    std::uint64_t imageRevision = 0;
    std::uint64_t cameraRevision = 0;
    QString name;
    QString sourcePath;
    bool visible = false;
    CameraShot shot;
    std::vector<RasterPlane> planes;
    int currentPlaneIndex = -1;
};

struct UndoState {
    std::vector<MeshSnapshot> meshes;
    std::vector<RasterSnapshot> rasters;
    int currentMeshIndex = -1;
    int currentRasterIndex = -1;
    CurrentLayerKind currentLayerKind = CurrentLayerKind::None;
    std::uint64_t nextMeshId = 1;
    std::uint64_t nextRasterId = 1;
    ViewState viewState;
};

// ---------------------------------------
// Action record (stored on undo nodes)
// ---------------------------------------

struct UndoActionRecord {
    QString kind;       // "filter", "load_mesh", "load_raster", "save_mesh", "load_project", ...
    QString filterKey;  // fully qualified filter key for filter actions
    QVariantMap params; // user-facing parameter values used for this action
    QStringList filePaths;
    QString pythonCall;
    QString compactPythonCall;
    int currentMeshIndex = -1;
    int currentRasterIndex = -1;
    CurrentLayerKind currentLayerKind = CurrentLayerKind::None;

    static UndoActionRecord fromScriptAction(const ScriptAction &action)
    {
        UndoActionRecord record;
        record.kind = action.kind;
        record.filterKey = action.filterKey;
        record.params = action.params;
        record.filePaths = action.filePaths;
        record.pythonCall = action.pythonCall;
        record.compactPythonCall = action.compactPythonCall;
        record.currentMeshIndex = action.currentMeshIndex;
        record.currentRasterIndex = action.currentRasterIndex;
        record.currentLayerKind = action.currentLayerKind;
        return record;
    }

    ScriptAction toScriptAction() const
    {
        ScriptAction action;
        action.kind = kind;
        action.filterKey = filterKey;
        action.params = params;
        action.filePaths = filePaths;
        action.pythonCall = pythonCall;
        action.compactPythonCall = compactPythonCall;
        action.currentMeshIndex = currentMeshIndex;
        action.currentRasterIndex = currentRasterIndex;
        action.currentLayerKind = currentLayerKind;
        return action;
    }
};

// ---------------------------------------
// Selection delta — packed bit flags
// ---------------------------------------

struct SelectionDelta {
    std::uint64_t meshId = 0;
    std::vector<std::uint32_t> vertexBits; // 1 bit per vertex, packed MSB-first
    std::vector<std::uint32_t> faceBits;   // 1 bit per face
};

// ---------------------------------------
// Storage kind — full vs delta
// ---------------------------------------

enum class UndoStorageKind {
    FullSnapshot,
    Delta
};

// ---------------------------------------
// Undo node — one node in the undo tree
// ---------------------------------------

// Tree-shaped undo history. Each node holds a full document
// restoration payload plus linkage (parentId, children, preferredChild).
// Node 0 is always the "before" root (initial state when recording started).
// m_undoCurrentNode is the id of the node that represents the current live state.
struct UndoNode {
    UndoState state;
    UndoStorageKind storageKind = UndoStorageKind::FullSnapshot;
    QString   label;         // label of the action that led INTO this node ("" for root)
    int       parentId = -1; // index into m_undoNodes (-1 for root)
    int       lane = 0;      // display lane assigned at creation time
    std::vector<int> children;
    int       preferredChild = -1; // which child to follow on redo() (-1 = none)

    // Optional action metadata.
    std::optional<UndoActionRecord> actionRecord;

    // Optional selection deltas (used when storageKind == Delta).
    // Storing compact bit flags instead of a full mesh deep-copy.
    std::optional<SelectionDelta> beforeSelection;
    std::optional<SelectionDelta> afterSelection;
};

// ---------------------------------------
// Display types
// ---------------------------------------

// Describes one node of the undo tree for display purposes.
// nodeId is stable for the lifetime of the node.  parentId == -1 for the root.
struct UndoTreeNodeInfo {
    int nodeId = -1;
    int parentId = -1;
    int depth = 0;   // 0 = root
    int lane = 0;    // display lane (column); 0 = main, 1+ = branches
    bool isCurrent = false;
    bool isOnCurrentPath = false; // lies on the path root → current node
    QString label;   // label of the action that produced this node ("" for root)
};

struct UndoStepMemoryInfo {
    QString label;
    qint64 beforeBytes = 0;
    qint64 afterBytes = 0;
    qint64 totalBytes() const { return beforeBytes + afterBytes; }
};

struct UndoMemoryStats {
    std::vector<UndoStepMemoryInfo> steps;
    qint64 totalBytes = 0;
};
