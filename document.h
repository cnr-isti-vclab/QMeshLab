#pragma once

#include "meshfilterplugin.h"
#include "meshioplugin.h"
#include "meshgpuresourcecache.h"
#include "vcgmesh.h"
#include <QObject>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <cstdint>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <optional>
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
        VCG
    };

    struct LogEntry {
        QString message;
        LogSource source = LogSource::Application;
    };

    struct MeshEntry {
        std::uint64_t meshId = 0;
        std::uint64_t geometryRevision = 0;
        std::uint64_t materialRevision = 0;
        QMatrix4x4 renderTransform;
        QString name;
        QString sourcePath;
        QStringList textureFileNames;
        QStringList textureFilePaths;
        bool visible = true;
        int ioMask = 0;
        VCGMesh mesh;
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
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool undo();
    bool redo();
    void clearUndoHistory();
    int undoLimit() const { return m_undoLimit; }
    void setUndoLimit(int limit);
    int addMesh(const VCGMesh &mesh, const QString &name = {}, int ioMask = 0);
    void removeMesh(int index);
    int duplicateMesh(int sourceIndex, const QString &newName = {});
    void setMeshVisible(int index, bool visible);
    QMatrix4x4 meshRenderTransform(int index) const;
    void setMeshRenderTransform(
        int index,
        const QMatrix4x4 &transform,
        const QString &contextMessage = {});
    void setCurrentMeshIndex(int index);
    void markMeshGeometryChanged(int index, const QString &contextMessage = {});
    void markMeshMaterialChanged(int index, const QString &contextMessage = {});
    void clearLog();
    void writeLog(const QString &message, LogSource source = LogSource::Application, bool replaceLast = false);

    int meshCount() const { return static_cast<int>(m_meshes.size()); }
    MeshEntry &mesh(int i) { return *m_meshes[i]; }
    const MeshEntry &mesh(int i) const { return *m_meshes[i]; }
    int currentMeshIndex() const { return m_currentMeshIndex; }
    const std::vector<LogEntry> &logMessages() const { return m_logMessages; }
    QString openDialogFilter() const;
    QString saveDialogFilter() const;
    int saveMaskCapability(const QString &filename) const;
    QStringList loadedPluginSummaries() const;
    QStringList loadedFilterPluginSummaries() const;
    std::vector<ImportPluginInfo> importPluginInfos() const;
    std::vector<ExportPluginInfo> exportPluginInfos() const;
    std::vector<FilterInfo> filterInfos() const;
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
                                bool needSelection = false);
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

signals:
    void meshAdded(int index);
    void meshRemoved(int index);
    void meshVisibilityChanged(int index, bool visible);
    void currentMeshChanged(int index);
    void meshDataChanged(int index);
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
        std::vector<std::unique_ptr<MeshEntry>> meshes;
        int currentMeshIndex = -1;
        std::uint64_t nextMeshId = 1;
    };

    struct UndoStep {
        QString label;
        UndoState before;
        UndoState after;
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
    void emitUndoRedoStateChanged();
    vcg::CallBackPos *logCallback();
    bool handleLogCallback(int pos, const char *message);
    static bool dispatchLogCallback(int pos, const char *message);
    void purgeMeshGpuResources(std::uint64_t meshId);

    std::unique_ptr<MeshIOPluginManager> m_pluginManager;
    std::unique_ptr<MeshFilterPluginManager> m_filterPluginManager;
    std::unique_ptr<MeshGpuResourceCache> m_gpuCache;
    std::vector<std::unique_ptr<MeshEntry>> m_meshes;
    std::uint64_t m_nextMeshId = 1;
    int m_currentMeshIndex = -1;
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
    std::vector<UndoStep> m_undoSteps;
    int m_undoCursor = 0;
    int m_undoLimit = 20;
    bool m_undoStepActive = false;
    QString m_undoStepLabel;
    std::optional<UndoState> m_pendingUndoBefore;
    bool m_restoringUndoRedo = false;
    CallbackMode m_callbackMode = CallbackMode::None;
    std::atomic<bool> m_cancelRequested = false;
};
