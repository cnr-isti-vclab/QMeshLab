#pragma once

#include "camerashot.h"
#include "meshfilterplugin.h"
#include "meshioplugin.h"
#include "meshgpuresourcecache.h"
#include "vcgmesh.h"
#include "viewstate.h"
#include <QObject>
#include <QElapsedTimer>
#include <QImage>
#include <QMatrix4x4>
#include <QSize>
#include <cstdint>
#include <functional>
#include <QString>
#include <QStringList>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

class MeshIOPluginManager;
class MeshFilterPluginManager;
class QRhi;
class QRhiCommandBuffer;

class Document : public QObject
{
    Q_OBJECT
public:
    enum class LogSource {
        Application,
        VCG,
        Error
    };
    enum class CurrentLayerKind {
        None,
        Mesh,
        Raster
    };

    struct LogEntry {
        QString message;
        LogSource source = LogSource::Application;
    };

    struct MeshEntry {
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
        bool visible = true;
        bool modified = false;
        int ioMask = 0;
        VCGMesh mesh;
    };

    enum class RasterPlaneSemantic {
        None = 0x0000,
        RGBA = 0x0001,
        MaskUInt8 = 0x0002,
        MaskFloat = 0x0004,
        DepthFloat = 0x0008,
        Extra00Float = 0x0100,
        Extra01Float = 0x0200,
        Extra02Float = 0x0400,
        Extra03Float = 0x0800,
        Extra00RGBA = 0x1000,
        Extra01RGBA = 0x2000,
        Extra02RGBA = 0x4000,
        Extra03RGBA = 0x8000
    };

    struct RasterPlane {
        RasterPlaneSemantic semantic = RasterPlaneSemantic::RGBA;
        QString name;
        QString sourcePath;
        QSize size;
        QImage image;

        bool hasImage() const { return !image.isNull(); }
        bool hasSourcePath() const { return !sourcePath.trimmed().isEmpty(); }
    };

    struct RasterEntry {
        std::uint64_t rasterId = 0;
        std::uint64_t imageRevision = 0;
        std::uint64_t cameraRevision = 0;
        QString name;
        QString sourcePath;
        bool visible = true;
        CameraShot shot;
        std::vector<RasterPlane> planes;
        int currentPlaneIndex = -1;

        RasterPlane *currentPlane()
        {
            return (currentPlaneIndex >= 0 && currentPlaneIndex < int(planes.size()))
                ? &planes[size_t(currentPlaneIndex)]
                : nullptr;
        }

        const RasterPlane *currentPlane() const
        {
            return (currentPlaneIndex >= 0 && currentPlaneIndex < int(planes.size()))
                ? &planes[size_t(currentPlaneIndex)]
                : nullptr;
        }
    };

    struct ImportPluginInfo {
        QString id;
        QString name;
        QStringList extensions;
    };

    struct ExportPluginInfo {
        QString id;
        QString name;
        QStringList extensions;
    };

    struct FilterInfo {
        QString key;
        QString pluginId;
        QString pluginName;
        MeshFilterDescriptor descriptor;
        bool applicable = true;
        QString applicabilityError;
    };

    struct CpuMeshMemoryStats {
        std::uint64_t meshId = 0;
        int meshIndex = -1;
        QString name;
        int vertexCapacity = 0;
        int edgeCapacity = 0;
        int faceCapacity = 0;
        qint64 vertexBytes = 0;
        qint64 vertexOcfBytes = 0;
        qint64 edgeBytes = 0;
        qint64 faceBytes = 0;
        qint64 faceOcfBytes = 0;
        qint64 totalBytes() const
        {
            return vertexBytes + vertexOcfBytes + edgeBytes + faceBytes + faceOcfBytes;
        }
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

    using FillGpuVariant = MeshGpuResourceCache::FillVariant;
    using PointGpuVariant = MeshGpuResourceCache::PointVariant;
    using FillBatchGpuView = MeshGpuResourceCache::FillBatchView;
    using FillPassGpuView = MeshGpuResourceCache::FillPassView;
    using WirePassGpuView = MeshGpuResourceCache::WirePassView;
    using EdgePassGpuView = MeshGpuResourceCache::EdgePassView;
    using EdgeFatPassGpuView = MeshGpuResourceCache::EdgeFatPassView;
    using PointsPassGpuView = MeshGpuResourceCache::PointsPassView;
    using BBoxPassGpuView = MeshGpuResourceCache::BBoxPassView;
    using SelectionPassGpuView = MeshGpuResourceCache::SelectionPassView;
    using DecoratorPassGpuView = MeshGpuResourceCache::DecoratorPassView;

    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    int loadMesh(const QString &filename);
    int reloadMesh(int index);
    int saveMesh(int index, const QString &filename, const MeshIOSaveOptions &options);
    int saveMesh(int index, const QString &filename);
    int saveCurrentMesh(const QString &filename, const MeshIOSaveOptions &options);
    int saveCurrentMesh(const QString &filename);
    void beginUndoStep(const QString &label);
    void endUndoStep(bool commit = true, bool restoreOnCancel = false);
    void setViewStateFunctions(std::function<ViewState()> capture,
                                std::function<void(const ViewState &, bool restoreCamera)> restore);
    void setRenderStateSnapshotFunction(
        std::function<bool(const QString &, const QSize &, QImage &, CameraShot &, QString &)> capture);
    bool renderSnapshotFromStateJson(
        const QString &renderStateJson,
        const QSize &pixelSize,
        QImage &outImage,
        CameraShot &outShot,
        QString *errorMessage = nullptr) const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isRestoringUndoRedo() const;
    QStringList undoHistoryLabels() const;
    QStringList undoStackLabels() const;
    int undoCursorPosition() const;
    std::vector<UndoTreeNodeInfo> undoTreeInfo() const;
    int undoCurrentNodeId() const;
    bool jumpToUndoNode(int nodeId, bool restoreCamera = true);
    bool updateUndoNodeCamera(int nodeId);
    bool makeUndoRoot(int nodeId);
    bool purgeUndoBranch(int nodeId);
    bool linearizeUndoHistory();
    bool undo();
    bool redo();
    void clearUndoHistory();
    int undoLimit() const { return m_undoLimit; }
    void setUndoLimit(int limit);
    int addMesh(const VCGMesh &mesh, const QString &name = {}, int ioMask = 0);
    void removeMesh(int index);
    int duplicateMesh(int sourceIndex, const QString &newName = {});
    int loadRasterImage(const QString &filename);
    int loadMeshLabProject(const QString &filename);
    int addRaster(const RasterEntry &raster);
    int addRasterImage(
        const QImage &image,
        const QString &name = {},
        const QString &sourcePath = {},
        const CameraShot &shot = CameraShot());
    void removeRaster(int index);
    void setMeshVisible(int index, bool visible);
    void setMeshName(int index, const QString &name);
    void setRasterVisible(int index, bool visible);
    void setRasterName(int index, const QString &name);
    void setRasterShot(int index, const CameraShot &shot, const QString &contextMessage = {});
    void setCurrentRasterIndex(int index);
    void setCurrentRasterPlaneIndex(int rasterIndex, int planeIndex);
    static void ensureRasterPlaneImage(RasterPlane &plane);
    void markRasterImageChanged(int index, const QString &contextMessage = {});
    QMatrix4x4 meshTransform(int index) const;
    void setMeshTransform(
        int index,
        const QMatrix4x4 &transform,
        const QString &contextMessage = {});
    void setCurrentMeshIndex(int index);
    void markMeshGeometryChanged(int index, const QString &contextMessage = {});
    void markMeshMaterialChanged(int index, const QString &contextMessage = {});
    // Selection is stored in per-vertex/per-face BitFlags, which are captured by the
    // undo geometry snapshot.  Changes to selection must therefore bump geometryRevision
    // so the undo cache produces a fresh deep-copy for the "after" checkpoint.
    void markMeshSelectionChanged(int index, const QString &contextMessage = {});
    void clearLog();
    void writeLog(const QString &message, LogSource source = LogSource::Application, bool replaceLast = false);

    int meshCount() const { return static_cast<int>(m_meshes.size()); }
    MeshEntry &mesh(int i) { return *m_meshes[i]; }
    const MeshEntry &mesh(int i) const { return *m_meshes[i]; }
    int rasterCount() const { return static_cast<int>(m_rasters.size()); }
    RasterEntry &raster(int i) { return *m_rasters[i]; }
    const RasterEntry &raster(int i) const { return *m_rasters[i]; }
    static int meshTextureAssociationCount(const MeshEntry &entry);
    static bool hasMeshTextureAssociation(const MeshEntry &entry);
    static QString meshTextureDisplayName(const MeshEntry &entry, int textureIndex);
    static QString meshTextureSourcePath(const MeshEntry &entry, int textureIndex);
    static const MeshIOTextureAsset *meshTextureAsset(const MeshEntry &entry, int textureIndex);
    static QString rasterPlaneDisplayName(const RasterPlane &plane, int planeIndex = 0);
    static QString rasterPlaneSourcePath(const RasterPlane &plane);
    int currentMeshIndex() const { return m_currentMeshIndex; }
    int currentRasterIndex() const { return m_currentRasterIndex; }
    CurrentLayerKind currentLayerKind() const { return m_currentLayerKind; }
    const std::vector<LogEntry> &logMessages() const { return m_logMessages; }
    QString openDialogFilter() const;
    QString saveDialogFilter() const;
    int saveMaskCapability(const QString &filename) const;
    QStringList loadedPluginSummaries() const;
    QStringList loadedFilterPluginSummaries() const;
    std::vector<ImportPluginInfo> importPluginInfos() const;
    std::vector<ExportPluginInfo> exportPluginInfos() const;
    std::vector<FilterInfo> filterInfos() const;
    bool validateFilterInvocation(
        const QString &filterKey,
        const MeshFilterParameterValues &parameters,
        QString &errorMessage) const;
    MeshFilterRunResult runFilter(
        const QString &filterKey,
        const MeshFilterParameterValues &parameters = {});
    vcg::CallBackPos *progressCallback();
    void beginFilterProgress(const QString &label);
    void finishFilterProgress(bool success, const QString &message);
    void requestOperationCancel();
    bool isOperationCancelRequested() const;
    QStringList importSupportedExtensions() const;
    QStringList exportSupportedExtensions() const;
    QString preferredImportPluginForExtension(const QString &extension) const;
    void setPreferredImportPluginForExtension(const QString &extension, const QString &pluginId);
    void ensureMeshGpuResources(QRhi *rhi,
                                QRhiCommandBuffer *cb,
                                int meshIndex,
                                FillGpuVariant fillVariant,
                                PointGpuVariant pointVariant,
                                bool needFill,
                                bool needWire,
                                bool needEdges,
                                bool needPoints,
                                bool needBoundingBox,
                                bool needDecoratorNormals,
                                bool needDecoratorBoundaries,
                                bool qualityFixedRange = false,
                                float qualityRangeMin = 0.0f,
                                float qualityRangeMax = 1.0f,
                                bool needSelection = false,
                                bool wireRespectFaux = true,
                                bool qualityCenterOnZero = false,
                                float qualityPercentileCrop = 0.0f);
    FillPassGpuView fillPassGpuView(QRhi *rhi, int meshIndex, FillGpuVariant variant) const;
    WirePassGpuView wirePassGpuView(QRhi *rhi, int meshIndex) const;
    EdgePassGpuView edgePassGpuView(QRhi *rhi, int meshIndex) const;
    EdgeFatPassGpuView edgeFatPassGpuView(QRhi *rhi, int meshIndex) const;
    PointsPassGpuView pointsPassGpuView(QRhi *rhi, int meshIndex, PointGpuVariant variant) const;
    BBoxPassGpuView bboxPassGpuView(QRhi *rhi, int meshIndex) const;
    SelectionPassGpuView selectionPassGpuView(QRhi *rhi, int meshIndex) const;
    DecoratorPassGpuView decoratorPassGpuView(QRhi *rhi, int meshIndex) const;
    void releaseRhiGpuResources(QRhi *rhi);
    void clearAllGpuResources();

    std::vector<CpuMeshMemoryStats> cpuMeshMemoryStats() const;
    UndoMemoryStats undoMemoryStats() const;
    std::vector<MeshGpuResourceCache::GpuMeshMemoryStats> gpuMemoryStats() const;

signals:
    void meshAdded(int index);
    void meshRemoved(int index);
    void meshVisibilityChanged(int index, bool visible);
    void currentMeshChanged(int index);
    void meshDataChanged(int index);
    void currentLayerChanged(Document::CurrentLayerKind kind, int index);
    void rasterAdded(int index);
    void rasterRemoved(int index);
    void rasterVisibilityChanged(int index, bool visible);
    void currentRasterChanged(int index);
    void rasterDataChanged(int index);
    void loadProgressStarted(const QString &filePath);
    void loadProgressUpdated(int percent, const QString &message);
    void loadProgressFinished(bool success, const QString &message);
    void filterProgressStarted(const QString &label);
    void filterProgressUpdated(int percent, const QString &message);
    void filterProgressFinished(bool success, const QString &message);
    void logCleared();
    void logMessageAdded(const QString &message, Document::LogSource source, bool replaceLast);
    void undoRedoStateChanged(
        bool canUndo,
        bool canRedo,
        const QString &undoText,
        const QString &redoText);

private:
    struct UndoState {
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

        std::vector<MeshSnapshot> meshes;
        std::vector<RasterSnapshot> rasters;
        int currentMeshIndex = -1;
        int currentRasterIndex = -1;
        CurrentLayerKind currentLayerKind = CurrentLayerKind::None;
        std::uint64_t nextMeshId = 1;
        std::uint64_t nextRasterId = 1;
        ViewState viewState;
    };

    // Tree-shaped undo history. Each node in m_undoNodes holds a full document
    // snapshot plus linkage (parentId, children, preferredChild).
    // Node 0 is always the "before" root (initial state when recording started).
    // m_undoCurrentNode is the id of the node that represents the current live state.
    // When a new action is committed, a new child is appended to the current node
    // instead of truncating siblings — so alternate timelines are preserved.
    struct UndoNode {
        UndoState state;
        QString   label;         // label of the action that led INTO this node ("" for root)
        int       parentId = -1; // index into m_undoNodes (-1 for root)
        int       lane = 0;      // display lane assigned at creation time
        std::vector<int> children;
        int       preferredChild = -1; // which child to follow on redo() (-1 = none)
    };

    enum class CallbackMode {
        None,
        Load,
        Filter,
        Save
    };

    UndoState captureUndoState() const;
    void restoreUndoState(const UndoState &state);
    void pushUndoStep(const QString &label, UndoState &&before, UndoState &&after);
    void pruneUndoTreeToLimit();
    void emitUndoRedoStateChanged();
    vcg::CallBackPos *logCallback();
    bool handleLogCallback(int pos, const char *message);
    static bool dispatchLogCallback(int pos, const char *message);
    void purgeMeshGpuResources(std::uint64_t meshId);

    std::unique_ptr<MeshIOPluginManager> m_pluginManager;
    std::unique_ptr<MeshFilterPluginManager> m_filterPluginManager;
    std::unique_ptr<MeshGpuResourceCache> m_gpuCache;
    std::vector<std::unique_ptr<MeshEntry>> m_meshes;
    std::vector<std::unique_ptr<RasterEntry>> m_rasters;
    std::uint64_t m_nextMeshId = 1;
    std::uint64_t m_nextRasterId = 1;
    // Globally monotonic counter for geometry revisions. Never reset or restored
    // during undo/redo, so every distinct geometry snapshot always gets a unique
    // (meshId, geometryRevision) key — preventing cross-branch cache collisions.
    std::uint64_t m_nextGeometryRevision = 1;
    void setCurrentMeshIndexInternal(int index, bool makeCurrentLayer);
    void setCurrentRasterIndexInternal(int index, bool makeCurrentLayer);
    int m_currentMeshIndex = -1;
    int m_currentRasterIndex = -1;
    CurrentLayerKind m_currentLayerKind = CurrentLayerKind::None;
    std::vector<LogEntry> m_logMessages;
    int m_lastCallbackBucket = -1;
    int m_lastProgressPos = -1;
    QElapsedTimer m_loadCallbackTimer;
    int m_loadCallbackCount = 0;
    int m_loadProgressEmitCount = 0;
    int m_loadProcessEventsCount = 0;
    qint64 m_loadProcessEventsNs = 0;
    qint64 m_lastProgressEmitMs = -1;
    qint64 m_lastProcessEventsMs = -1;
    // Interning cache for undo geometry objects, keyed by (meshId, geometryRevision).
    // Maps to a weak_ptr so the cache never artificially extends the lifetime of a
    // geometry beyond the checkpoints that reference it — entries expire automatically
    // when the last referencing checkpoint is dropped (e.g. after undo-limit eviction).
    mutable std::map<std::pair<std::uint64_t, std::uint64_t>, std::weak_ptr<const VCGMesh>>
        m_undoGeometryCache;

    std::vector<UndoNode> m_undoNodes;   // flat arena; index == node id
    int m_undoCurrentNode = -1;          // id of the node representing live state (-1 = no history)
    int m_undoLimit = 20;
    bool m_undoStepActive = false;
    QString m_undoStepLabel;
    std::optional<UndoState> m_pendingUndoBefore;
    bool m_restoringUndoRedo = false;
    bool m_suppressUndoRedoSignals = false;
    bool m_restoreCamera = true;
    CallbackMode m_callbackMode = CallbackMode::None;
    std::atomic<bool> m_cancelRequested = false;
    std::function<ViewState()> m_captureViewState;
    std::function<void(const ViewState &, bool restoreCamera)> m_restoreViewState;
    std::function<bool(const QString &, const QSize &, QImage &, CameraShot &, QString &)> m_captureRenderStateSnapshot;
    bool m_bulkLoading = false;
};
