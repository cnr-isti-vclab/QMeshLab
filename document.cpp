#include "document.h"
#include "meshiopluginmanager.h"
#include "plugins/meshpluginregistry.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <QElapsedTimer>
#include <QFileInfo>

namespace {
Document *g_callbackDocument = nullptr;

QString summarizeLoadMask(int mask)
{
    using Mask = vcg::tri::io::Mask;

    QStringList attrs;
    auto addIf = [&attrs, mask](int bit, const QString &name) {
        if ((mask & bit) != 0)
            attrs.append(name);
    };

    addIf(Mask::IOM_VERTCOLOR, QStringLiteral("vertex color"));
    addIf(Mask::IOM_FACECOLOR, QStringLiteral("face color"));
    addIf(Mask::IOM_VERTNORMAL, QStringLiteral("vertex normal"));
    addIf(Mask::IOM_FACENORMAL, QStringLiteral("face normal"));
    addIf(Mask::IOM_VERTTEXCOORD, QStringLiteral("vertex texcoord"));
    addIf(Mask::IOM_WEDGTEXCOORD, QStringLiteral("wedge texcoord"));
    addIf(Mask::IOM_WEDGTEXMULTI, QStringLiteral("multi texture index"));
    addIf(Mask::IOM_WEDGCOLOR, QStringLiteral("wedge color"));
    addIf(Mask::IOM_WEDGNORMAL, QStringLiteral("wedge normal"));
    addIf(Mask::IOM_VERTQUALITY, QStringLiteral("vertex quality"));
    addIf(Mask::IOM_FACEQUALITY, QStringLiteral("face quality"));
    addIf(Mask::IOM_VERTRADIUS, QStringLiteral("vertex radius"));
    addIf(Mask::IOM_EDGEINDEX, QStringLiteral("edge index"));
    addIf(Mask::IOM_BITPOLYGONAL, QStringLiteral("polygonal faces"));
    addIf(Mask::IOM_CAMERA, QStringLiteral("camera"));

    if (attrs.isEmpty())
        return QObject::tr("none");
    return attrs.join(QStringLiteral(", "));
}
}

Document::Document(QObject *parent)
    : QObject(parent)
    , m_pluginManager(std::make_unique<MeshIOPluginManager>())
{
    registerBuiltinMeshPlugins(*m_pluginManager);
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
    int loadMask = 0;
    int err = plugin->load(filename, entry->mesh, logCallback(), &loadMask);
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
    const bool hasImportedVertexNormals = (loadMask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0;
    if (!hasImportedVertexNormals && entry->mesh.FN() > 0) {
        // Preserve imported vertex normals exactly as provided by the file.
        // Generate smooth normals only when they are missing.
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry->mesh);
    }
    entry->ioMask = loadMask;
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
    writeLog(tr("File info for '%1': %2 (mask: 0x%3)")
        .arg(meshEntry.name)
        .arg(summarizeLoadMask(loadMask))
        .arg(QString::number(static_cast<quint32>(loadMask), 16).toUpper()),
        LogSource::Application);

    emit meshAdded(index);
    setCurrentMeshIndex(index);
    return 0;
}

void Document::removeMesh(int index)
{
    if (index < 0 || index >= meshCount())
        return;

    const QString meshName = mesh(index).name;
    int newCurrent = m_currentMeshIndex;
    if (m_currentMeshIndex == index) {
        if (meshCount() == 1)
            newCurrent = -1;
        else
            newCurrent = (index < meshCount() - 1) ? index : (meshCount() - 2);
    } else if (m_currentMeshIndex > index) {
        newCurrent = m_currentMeshIndex - 1;
    }

    m_meshes.erase(m_meshes.begin() + index);
    writeLog(tr("Removed mesh '%1'").arg(meshName), LogSource::Application);
    emit meshRemoved(index);
    setCurrentMeshIndex(newCurrent);
}

void Document::setMeshVisible(int index, bool visible)
{
    if (index < 0 || index >= meshCount())
        return;
    MeshEntry &entry = mesh(index);
    if (entry.visible == visible)
        return;
    entry.visible = visible;
    emit meshVisibilityChanged(index, visible);
}

void Document::setCurrentMeshIndex(int index)
{
    const int normalizedIndex = (index >= 0 && index < meshCount()) ? index : -1;
    if (m_currentMeshIndex == normalizedIndex)
        return;
    m_currentMeshIndex = normalizedIndex;
    emit currentMeshChanged(m_currentMeshIndex);
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
