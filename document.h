#pragma once

#include "vcgmesh.h"
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

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
        VCGMesh mesh;
    };

    explicit Document(QObject *parent = nullptr);

    int loadMesh(const QString &filename);
    void removeMesh(int index);
    void clearLog();
    void writeLog(const QString &message, LogSource source = LogSource::Application, bool replaceLast = false);

    int meshCount() const { return static_cast<int>(m_meshes.size()); }
    MeshEntry &mesh(int i) { return *m_meshes[i]; }
    const MeshEntry &mesh(int i) const { return *m_meshes[i]; }
    const std::vector<LogEntry> &logMessages() const { return m_logMessages; }
    vcg::CallBackPos *logCallback();

signals:
    void meshAdded(int index);
    void meshRemoved(int index);
    void logCleared();
    void logMessageAdded(const QString &message, Document::LogSource source, bool replaceLast);

private:
    bool handleLogCallback(int pos, const char *message);
    static bool dispatchLogCallback(int pos, const char *message);

    std::vector<std::unique_ptr<MeshEntry>> m_meshes;
    std::vector<LogEntry> m_logMessages;
    QString m_lastCallbackMessage;
    int m_lastCallbackBucket = -1;
};
