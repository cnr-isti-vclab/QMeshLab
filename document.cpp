#include "document.h"
#include "meshiopluginmanager.h"
#include "plugins/meshpluginregistry.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
#include <QFileInfo>
#include <algorithm>

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

QString resolveTexturePath(const QString &meshFilePath, const QString &declaredTextureName)
{
    const QString normalizedName = QDir::fromNativeSeparators(declaredTextureName);
    QFileInfo textureInfo(normalizedName);
    if (textureInfo.isAbsolute())
        return textureInfo.absoluteFilePath();

    const QFileInfo meshInfo(meshFilePath);
    return meshInfo.dir().filePath(normalizedName);
}

}

Document::Document(QObject *parent)
    : QObject(parent)
    , m_pluginManager(std::make_unique<MeshIOPluginManager>())
    , m_gpuCache(std::make_unique<MeshGpuResourceCache>())
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

std::vector<Document::ImportPluginInfo> Document::importPluginInfos() const
{
    std::vector<ImportPluginInfo> infos;
    if (!m_pluginManager)
        return infos;

    const auto pluginInfos = m_pluginManager->pluginInfos();
    infos.reserve(pluginInfos.size());
    for (const auto &info : pluginInfos) {
        ImportPluginInfo out;
        out.id = info.id;
        out.name = info.name;
        out.extensions = info.extensions;
        infos.push_back(std::move(out));
    }
    return infos;
}

QStringList Document::importSupportedExtensions() const
{
    if (!m_pluginManager)
        return {};
    return m_pluginManager->supportedExtensions();
}

QString Document::preferredImportPluginForExtension(const QString &extension) const
{
    if (!m_pluginManager)
        return {};
    return m_pluginManager->preferredPluginForExtension(extension);
}

void Document::setPreferredImportPluginForExtension(const QString &extension, const QString &pluginId)
{
    if (!m_pluginManager)
        return;
    m_pluginManager->setPreferredPluginForExtension(extension, pluginId);
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
    m_lastCallbackBucket = -1;
    m_lastProgressPos = -1;
    m_loadCallbackCount = 0;
    m_loadProgressEmitCount = 0;
    m_loadProcessEventsCount = 0;
    m_loadProcessEventsNs = 0;
    m_lastProgressEmitMs = -1;
    m_lastProcessEventsMs = -1;
    m_loadCallbackTimer.invalidate();
    m_loadCallbackTimer.start();
    QElapsedTimer loadTimer;
    loadTimer.start();
    emit loadProgressStarted(filename);
    emit loadProgressUpdated(0, tr("Opening %1").arg(QFileInfo(filename).fileName()));

    Document *previousCallbackDocument = g_callbackDocument;
    g_callbackDocument = this;
    int loadMask = 0;
    int err = plugin->load(filename, entry->mesh, logCallback(), &loadMask);
    g_callbackDocument = previousCallbackDocument;
    const qint64 importElapsedMs = loadTimer.elapsed();

    if (err != 0) {
        emit loadProgressFinished(false, plugin->errorString(err));
        writeLog(tr("Load failed in %1 ms: %2")
            .arg(importElapsedMs)
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
    entry->meshId = m_nextMeshId++;
    entry->geometryRevision = 1;
    entry->materialRevision = 1;
    entry->name = QFileInfo(filename).fileName();
    entry->sourcePath = filename;

    QStringList declaredTextureNames;
    QStringList resolvedTexturePaths;
    QString selectedTextureName;
    bool selectedExistingTexture = false;
    for (const std::string &rawTextureName : entry->mesh.textures) {
        const QString textureName = QString::fromStdString(rawTextureName).trimmed();
        if (textureName.isEmpty())
            continue;
        declaredTextureNames.append(textureName);
        const QString resolvedTexturePath = resolveTexturePath(filename, textureName);
        resolvedTexturePaths.append(resolvedTexturePath);
        entry->textureFileNames.append(textureName);
        entry->textureFilePaths.append(resolvedTexturePath);
        if (selectedTextureName.isEmpty()) {
            selectedTextureName = textureName;
        }
        if (!selectedExistingTexture && QFileInfo::exists(resolvedTexturePath)) {
            selectedTextureName = textureName;
            selectedExistingTexture = true;
        }
    }

    int index = meshCount();
    m_meshes.push_back(std::move(entry));

    const qint64 elapsedMs = loadTimer.elapsed();
    const qint64 postProcessElapsedMs = std::max<qint64>(0, elapsedMs - importElapsedMs);
    const MeshEntry &meshEntry = mesh(index);
    writeLog(tr("Loaded mesh '%1' in %2 ms (%3 vertices, %4 faces, %5 edges)")
        .arg(meshEntry.name)
        .arg(elapsedMs)
        .arg(meshEntry.mesh.VN())
        .arg(meshEntry.mesh.FN())
        .arg(meshEntry.mesh.EN()),
        LogSource::Application);
    if (elapsedMs >= 250) {
        writeLog(tr("Load timing '%1': import %2 ms, post %3 ms")
                .arg(meshEntry.name)
                .arg(importElapsedMs)
                .arg(postProcessElapsedMs),
            LogSource::Application);
    }
    if (m_loadCallbackCount > 0) {
        const float processEventsMs = float(m_loadProcessEventsNs / 1000000.0);
        writeLog(tr("Load callback stats '%1': %2 calls, %3 UI updates, %4 event pumps (%5 ms)")
                .arg(meshEntry.name)
                .arg(m_loadCallbackCount)
                .arg(m_loadProgressEmitCount)
                .arg(m_loadProcessEventsCount)
                .arg(QString::number(processEventsMs, 'f', 2)),
            LogSource::Application);
    }
    writeLog(tr("File info for '%1': %2 (mask: 0x%3)")
        .arg(meshEntry.name)
        .arg(summarizeLoadMask(loadMask))
        .arg(QString::number(static_cast<quint32>(loadMask), 16).toUpper()),
        LogSource::Application);
    if (!declaredTextureNames.isEmpty()) {
        int existingTextureFiles = 0;
        for (const QString &path : meshEntry.textureFilePaths) {
            if (QFileInfo::exists(path))
                ++existingTextureFiles;
        }
        writeLog(tr("Texture info for '%1': declared [%2], resolved [%3], selected '%4' (%5/%6 files found)")
            .arg(meshEntry.name)
            .arg(declaredTextureNames.join(QStringLiteral(", ")))
            .arg(resolvedTexturePaths.join(QStringLiteral(", ")))
            .arg(selectedTextureName.isEmpty() ? tr("none") : selectedTextureName)
            .arg(existingTextureFiles)
            .arg(meshEntry.textureFilePaths.size()),
            LogSource::Application);
    }

    emit meshAdded(index);
    setCurrentMeshIndex(index);
    emit loadProgressUpdated(100, tr("Loaded %1").arg(meshEntry.name));
    emit loadProgressFinished(true, tr("Loaded %1").arg(meshEntry.name));
    return 0;
}

void Document::removeMesh(int index)
{
    if (index < 0 || index >= meshCount())
        return;

    const QString meshName = mesh(index).name;
    const std::uint64_t meshId = mesh(index).meshId;
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
    purgeMeshGpuResources(meshId);
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

void Document::ensureMeshGpuResources(QRhi *rhi,
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
                                      bool needDecoratorBoundaries)
{
    if (!m_gpuCache || !rhi || !cb)
        return;
    if (meshIndex < 0 || meshIndex >= meshCount())
        return;

    const MeshEntry &meshEntry = mesh(meshIndex);
    MeshGpuResourceCache::MeshSource source;
    source.meshId = meshEntry.meshId;
    source.geometryRevision = meshEntry.geometryRevision;
    source.materialRevision = meshEntry.materialRevision;
    source.ioMask = meshEntry.ioMask;
    source.mesh = &meshEntry.mesh;
    source.textureFilePaths = &meshEntry.textureFilePaths;

    const MeshGpuResourceCache::EnsureStats stats = m_gpuCache->ensureMeshResources(
        rhi,
        cb,
        source,
        fillVariant,
        pointVariant,
        needFill,
        needWire,
        needEdges,
        needPoints,
        needBoundingBox,
        needDecoratorNormals,
        needDecoratorBoundaries);

    if (stats.anyRebuilt()) {
        QStringList rebuiltPasses;
        if (stats.rebuiltFill)
            rebuiltPasses << tr("fill");
        if (stats.rebuiltWire)
            rebuiltPasses << tr("wire");
        if (stats.rebuiltEdges)
            rebuiltPasses << tr("edges");
        if (stats.rebuiltPoints)
            rebuiltPasses << tr("points");
        if (stats.rebuiltBoundingBox)
            rebuiltPasses << tr("bbox");
        if (stats.rebuiltDecoratorNormals)
            rebuiltPasses << tr("decorator normals");
        if (stats.rebuiltDecoratorBoundaries)
            rebuiltPasses << tr("decorator boundaries");
        writeLog(
            tr("GPU buffers built for '%1': %2 in %3 ms")
                .arg(meshEntry.name)
                .arg(rebuiltPasses.join(QStringLiteral(", ")))
                .arg(QString::number(stats.elapsedMs, 'f', 2)),
            LogSource::Application);
    }
}

Document::FillPassGpuView Document::fillPassGpuView(
    QRhi *rhi, int meshIndex, FillGpuVariant variant) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->fillPassView(rhi, meshEntry.meshId, variant);
}

Document::WirePassGpuView Document::wirePassGpuView(QRhi *rhi, int meshIndex) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->wirePassView(rhi, meshEntry.meshId);
}

Document::EdgePassGpuView Document::edgePassGpuView(QRhi *rhi, int meshIndex) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->edgePassView(rhi, meshEntry.meshId);
}

Document::PointsPassGpuView Document::pointsPassGpuView(
    QRhi *rhi, int meshIndex, PointGpuVariant variant) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->pointsPassView(rhi, meshEntry.meshId, variant);
}

Document::BBoxPassGpuView Document::bboxPassGpuView(QRhi *rhi, int meshIndex) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->bboxPassView(rhi, meshEntry.meshId);
}

Document::DecoratorPassGpuView Document::decoratorPassGpuView(
    QRhi *rhi, int meshIndex) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->decoratorPassView(rhi, meshEntry.meshId);
}

void Document::releaseRhiGpuResources(QRhi *rhi)
{
    if (!m_gpuCache || !rhi)
        return;
    m_gpuCache->releaseRhiResources(rhi);
}

void Document::clearAllGpuResources()
{
    if (!m_gpuCache)
        return;
    m_gpuCache->clearAll();
}

void Document::purgeMeshGpuResources(std::uint64_t meshId)
{
    if (!m_gpuCache)
        return;
    m_gpuCache->purgeMesh(meshId);
}

void Document::clearLog()
{
    if (m_logMessages.empty())
        return;

    m_logMessages.clear();
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
    ++m_loadCallbackCount;

    const int clampedPos = std::clamp(pos, 0, 100);
    const qint64 nowMs = m_loadCallbackTimer.isValid() ? m_loadCallbackTimer.elapsed() : 0;
    const bool forceUiUpdate = (clampedPos == 0 || clampedPos == 100);
    const bool progressChanged = (clampedPos != m_lastProgressPos);
    const bool uiThrottleElapsed =
        (m_lastProgressEmitMs < 0) || (nowMs - m_lastProgressEmitMs >= 33);

    QString text;
    bool textDecoded = false;
    auto decodeText = [&]() {
        if (textDecoded)
            return;
        const QByteArray rawMessage(message ? message : "");
        text = QString::fromLocal8Bit(rawMessage);
        while (!text.isEmpty() && (text.endsWith('\n') || text.endsWith('\r')))
            text.chop(1);
        text = text.trimmed();
        textDecoded = true;
    };

    if (progressChanged && (forceUiUpdate || uiThrottleElapsed)) {
        m_lastProgressPos = clampedPos;
        m_lastProgressEmitMs = nowMs;
        decodeText();
        emit loadProgressUpdated(clampedPos, text);
        ++m_loadProgressEmitCount;

        const bool processEventsThrottleElapsed =
            (m_lastProcessEventsMs < 0) || (nowMs - m_lastProcessEventsMs >= 80);
        if (forceUiUpdate || processEventsThrottleElapsed) {
            QElapsedTimer processTimer;
            processTimer.start();
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            m_loadProcessEventsNs += processTimer.nsecsElapsed();
            m_lastProcessEventsMs = nowMs;
            ++m_loadProcessEventsCount;
        }
    }

    const int bucket = clampedPos / 10;
    if (bucket == m_lastCallbackBucket)
        return true;

    m_lastCallbackBucket = bucket;
    decodeText();
    const bool replaceLast = message ? QByteArray(message).endsWith('\r') : false;

    if (text.isEmpty())
        writeLog(tr("Progress %1%").arg(clampedPos), LogSource::VCG, replaceLast);
    else
        writeLog(tr("%1% - %2").arg(clampedPos, 3).arg(text), LogSource::VCG, replaceLast);

    return true;
}

bool Document::dispatchLogCallback(int pos, const char *message)
{
    if (!g_callbackDocument)
        return true;

    return g_callbackDocument->handleLogCallback(pos, message);
}
