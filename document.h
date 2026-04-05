#pragma once

#include "vcgmesh.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class MeshIOPluginManager;

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
        QString name;
        bool visible = true;
        int ioMask = 0;
        VCGMesh mesh;
    };

    explicit Document(QObject *parent = nullptr);
    ~Document() override;

    int loadMesh(const QString &filename);
    void removeMesh(int index);
    void setMeshVisible(int index, bool visible);
    void setCurrentMeshIndex(int index);
    void clearLog();
    void writeLog(const QString &message, LogSource source = LogSource::Application, bool replaceLast = false);

    int meshCount() const { return static_cast<int>(m_meshes.size()); }
    MeshEntry &mesh(int i) { return *m_meshes[i]; }
    const MeshEntry &mesh(int i) const { return *m_meshes[i]; }
    int currentMeshIndex() const { return m_currentMeshIndex; }
    const std::vector<LogEntry> &logMessages() const { return m_logMessages; }
    QString openDialogFilter() const;
    QStringList loadedPluginSummaries() const;

signals:
    void meshAdded(int index);
    void meshRemoved(int index);
    void meshVisibilityChanged(int index, bool visible);
    void currentMeshChanged(int index);
    void logCleared();
    void logMessageAdded(const QString &message, Document::LogSource source, bool replaceLast);

private:
    vcg::CallBackPos *logCallback();
    bool handleLogCallback(int pos, const char *message);
    static bool dispatchLogCallback(int pos, const char *message);

    std::unique_ptr<MeshIOPluginManager> m_pluginManager;
    std::vector<std::unique_ptr<MeshEntry>> m_meshes;
    int m_currentMeshIndex = -1;
    std::vector<LogEntry> m_logMessages;
    QString m_lastCallbackMessage;
    int m_lastCallbackBucket = -1;
};
