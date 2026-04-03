#include "document.h"
#include "meshiopluginmanager.h"
#include "vcgimportplugin.h"
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <QElapsedTimer>
#include <QFileInfo>

namespace {
Document *g_callbackDocument = nullptr;
}

Document::Document(QObject *parent)
    : QObject(parent)
    , m_pluginManager(std::make_unique<MeshIOPluginManager>())
{
    m_pluginManager->registerPlugin(std::make_unique<VCGImportPlugin>());
}

Document::~Document() = default;

QString Document::openDialogFilter() const
{
    return m_pluginManager->openDialogFilter();
}

QStringList Document::loadedPluginSummaries() const
{
    return m_pluginManager->loadedPluginSummaries();
}

int Document::loadMesh(const QString &filename)
{
    const MeshIOPlugin *plugin = m_pluginManager->pluginFor(filename);
    if (!plugin) {
        writeLog(tr("No plugin found for: %1").arg(filename), LogSource::Application);
        return -1;
    }

    writeLog(tr("Loading mesh: %1").arg(filename), LogSource::Application);

    auto entry = std::make_unique<MeshEntry>();
    m_lastCallbackMessage.clear();
    m_lastCallbackBucket = -1;
    QElapsedTimer loadTimer;
    loadTimer.start();

    Document *previousCallbackDocument = g_callbackDocument;
    g_callbackDocument = this;
    int err = plugin->load(filename, entry->mesh, logCallback());
    g_callbackDocument = previousCallbackDocument;
    const qint64 elapsedMs = loadTimer.elapsed();

    if (err != 0) {
        writeLog(tr("Load failed in %1 ms: %2")
            .arg(elapsedMs)
            .arg(plugin->errorString(err)),
            LogSource::Application);
        return err;
    }

    vcg::tri::UpdateBounding<VCGMesh>::Box(entry->mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry->mesh);
    entry->name = QFileInfo(filename).fileName();
    int index = meshCount();
    m_meshes.push_back(std::move(entry));

    const MeshEntry &meshEntry = mesh(index);
    writeLog(tr("Loaded mesh '%1' in %2 ms (%3 vertices, %4 faces)")
        .arg(meshEntry.name)
        .arg(elapsedMs)
        .arg(meshEntry.mesh.VN())
        .arg(meshEntry.mesh.FN()),
        LogSource::Application);

    emit meshAdded(index);
    return 0;
}

void Document::removeMesh(int index)
{
    if (index < 0 || index >= meshCount())
        return;

    const QString meshName = mesh(index).name;
    m_meshes.erase(m_meshes.begin() + index);
    writeLog(tr("Removed mesh '%1'").arg(meshName), LogSource::Application);
    emit meshRemoved(index);
}

void Document::clearLog()
{
    if (m_logMessages.empty())
        return;

    m_logMessages.clear();
    m_lastCallbackMessage.clear();
    m_lastCallbackBucket = -1;
    emit logCleared();
}

void Document::writeLog(const QString &message, LogSource source, bool replaceLast)
{
    QString normalizedMessage = message;
    if (!replaceLast && normalizedMessage.endsWith('\r'))
        replaceLast = true;

    while (!normalizedMessage.isEmpty() && (normalizedMessage.endsWith('\n') || normalizedMessage.endsWith('\r')))
        normalizedMessage.chop(1);
    normalizedMessage = normalizedMessage.trimmed();

    if (normalizedMessage.isEmpty())
        return;

    if (replaceLast && !m_logMessages.empty()) {
        m_logMessages.back() = LogEntry{normalizedMessage, source};
    } else {
        m_logMessages.push_back(LogEntry{normalizedMessage, source});
    }

    emit logMessageAdded(normalizedMessage, source, replaceLast);
}

vcg::CallBackPos *Document::logCallback()
{
    return &Document::dispatchLogCallback;
}

bool Document::handleLogCallback(int pos, const char *message)
{
    const QByteArray rawMessage(message ? message : "");
    QString text = QString::fromLocal8Bit(rawMessage);
    const bool replaceLast = text.endsWith('\r');
    while (!text.isEmpty() && (text.endsWith('\n') || text.endsWith('\r')))
        text.chop(1);
    text = text.trimmed();

    const int bucket = pos / 10;

    if (text == m_lastCallbackMessage && bucket == m_lastCallbackBucket)
        return true;

    m_lastCallbackMessage = text;
    m_lastCallbackBucket = bucket;

    if (text.isEmpty())
        writeLog(tr("Progress %1%").arg(pos), LogSource::VCG, replaceLast);
    else
        writeLog(tr("%1% - %2").arg(pos, 3).arg(text), LogSource::VCG, replaceLast);

    return true;
}

bool Document::dispatchLogCallback(int pos, const char *message)
{
    if (!g_callbackDocument)
        return true;

    return g_callbackDocument->handleLogCallback(pos, message);
}
