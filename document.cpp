#include "document.h"
#include "meshfilterpluginmanager.h"
#include "meshiopluginmanager.h"
#include "plugins/filterpluginregistry.h"
#include "plugins/meshpluginregistry.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
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

void copyMeshEntryMetadata(const Document::MeshEntry &src, Document::MeshEntry &dst)
{
    dst.meshId = src.meshId;
    dst.geometryRevision = src.geometryRevision;
    dst.materialRevision = src.materialRevision;
    dst.renderTransform = src.renderTransform;
    dst.name = src.name;
    dst.sourcePath = src.sourcePath;
    dst.textureFileNames = src.textureFileNames;
    dst.textureFilePaths = src.textureFilePaths;
    dst.visible = src.visible;
    dst.ioMask = src.ioMask;
}

void deepCopyMesh(const VCGMesh &src, VCGMesh &dst)
{
    dst.Clear();

    std::vector<int> vertexMap(src.vert.size(), -1);
    if (src.VN() > 0) {
        vcg::tri::Allocator<VCGMesh>::AddVertices(dst, src.VN());
        int dstVertexIndex = 0;
        for (size_t i = 0; i < src.vert.size(); ++i) {
            const VCGVertex &sv = src.vert[i];
            if (sv.IsD())
                continue;
            VCGVertex &dv = dst.vert[static_cast<size_t>(dstVertexIndex)];
            dv.P() = sv.cP();
            dv.N() = sv.cN();
            dv.T() = sv.cT();
            dv.C() = sv.cC();
            dv.Q() = sv.cQ();
            dv.Flags() = sv.Flags();
            vertexMap[i] = dstVertexIndex;
            ++dstVertexIndex;
        }
    }

    const VCGVertex *srcVertexBase = src.vert.empty() ? nullptr : &src.vert.front();
    if (src.FN() > 0) {
        vcg::tri::Allocator<VCGMesh>::AddFaces(dst, src.FN());
        int dstFaceIndex = 0;
        for (const VCGFace &sf : src.face) {
            if (sf.IsD())
                continue;
            VCGFace &df = dst.face[static_cast<size_t>(dstFaceIndex)];
            for (int k = 0; k < 3; ++k) {
                const VCGVertex *sv = sf.cV(k);
                VCGVertex *dv = nullptr;
                if (sv && srcVertexBase) {
                    const ptrdiff_t srcIdx = sv - srcVertexBase;
                    if (srcIdx >= 0 && static_cast<size_t>(srcIdx) < vertexMap.size()) {
                        const int dstIdx = vertexMap[static_cast<size_t>(srcIdx)];
                        if (dstIdx >= 0)
                            dv = &dst.vert[static_cast<size_t>(dstIdx)];
                    }
                }
                df.V(k) = dv;
                df.WT(k) = sf.cWT(k);
            }
            df.N() = sf.cN();
            df.C() = sf.cC();
            df.Q() = sf.cQ();
            df.Flags() = sf.Flags();
            ++dstFaceIndex;
        }
    }

    if (src.EN() > 0) {
        vcg::tri::Allocator<VCGMesh>::AddEdges(dst, src.EN());
        int dstEdgeIndex = 0;
        for (const VCGEdge &se : src.edge) {
            if (se.IsD())
                continue;
            VCGEdge &de = dst.edge[static_cast<size_t>(dstEdgeIndex)];
            for (int k = 0; k < 2; ++k) {
                const VCGVertex *sv = se.cV(k);
                VCGVertex *dv = nullptr;
                if (sv && srcVertexBase) {
                    const ptrdiff_t srcIdx = sv - srcVertexBase;
                    if (srcIdx >= 0 && static_cast<size_t>(srcIdx) < vertexMap.size()) {
                        const int dstIdx = vertexMap[static_cast<size_t>(srcIdx)];
                        if (dstIdx >= 0)
                            dv = &dst.vert[static_cast<size_t>(dstIdx)];
                    }
                }
                de.V(k) = dv;
            }
            de.Flags() = se.Flags();
            ++dstEdgeIndex;
        }
    }

    dst.bbox = src.bbox;
    dst.textures = src.textures;
}

}

Document::Document(QObject *parent)
    : QObject(parent)
    , m_pluginManager(std::make_unique<MeshIOPluginManager>())
    , m_filterPluginManager(std::make_unique<MeshFilterPluginManager>())
    , m_gpuCache(std::make_unique<MeshGpuResourceCache>())
{
    registerBuiltinMeshPlugins(*m_pluginManager);
    registerBuiltinMeshFilterPlugins(*m_filterPluginManager);
}

Document::~Document() = default;

QString Document::openDialogFilter() const
{
    return m_pluginManager->openDialogFilter();
}

QString Document::saveDialogFilter() const
{
    return m_pluginManager->saveDialogFilter();
}

int Document::saveMaskCapability(const QString &filename) const
{
    const QString normalizedFilename = filename.trimmed();
    if (normalizedFilename.isEmpty())
        return 0;
    const MeshIOPlugin *plugin = m_pluginManager->pluginForSave(normalizedFilename);
    if (!plugin)
        return 0;
    return plugin->saveMaskCapability(normalizedFilename);
}

QStringList Document::loadedPluginSummaries() const
{
    return m_pluginManager->loadedPluginSummaries();
}

QStringList Document::loadedFilterPluginSummaries() const
{
    if (!m_filterPluginManager)
        return {};
    return m_filterPluginManager->loadedPluginSummaries();
}

std::vector<Document::FilterInfo> Document::filterInfos() const
{
    std::vector<FilterInfo> infos;
    if (!m_filterPluginManager)
        return infos;

    const auto managedInfos = m_filterPluginManager->filterInfos(*this);
    infos.reserve(managedInfos.size());
    for (const auto &managedInfo : managedInfos) {
        FilterInfo info;
        info.key = managedInfo.key;
        info.pluginId = managedInfo.pluginId;
        info.pluginName = managedInfo.pluginName;
        info.descriptor = managedInfo.descriptor;
        info.applicable = managedInfo.applicable;
        info.applicabilityError = managedInfo.applicabilityError;
        infos.push_back(std::move(info));
    }
    return infos;
}

MeshFilterRunResult Document::runFilter(
    const QString &filterKey,
    const MeshFilterParameterValues &parameters)
{
    if (!m_filterPluginManager) {
        return {
            false,
            false,
            tr("No filter plugin manager is available.")
        };
    }

    Document *previousCallbackDocument = g_callbackDocument;
    g_callbackDocument = this;
    MeshFilterRunResult result = m_filterPluginManager->runFilter(filterKey, parameters, *this);
    g_callbackDocument = previousCallbackDocument;
    return result;
}

vcg::CallBackPos *Document::progressCallback()
{
    return logCallback();
}

void Document::beginFilterProgress(const QString &label)
{
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
    m_cancelRequested.store(false, std::memory_order_relaxed);
    m_callbackMode = CallbackMode::Filter;

    const QString normalizedLabel = label.trimmed().isEmpty() ? tr("Filter") : label.trimmed();
    emit filterProgressStarted(normalizedLabel);
    emit filterProgressUpdated(0, normalizedLabel);
}

void Document::finishFilterProgress(bool success, const QString &message)
{
    const QString normalizedMessage = message.trimmed();
    m_callbackMode = CallbackMode::None;
    m_cancelRequested.store(false, std::memory_order_relaxed);
    emit filterProgressFinished(success, normalizedMessage);
}

void Document::requestOperationCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool Document::isOperationCancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_relaxed);
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

std::vector<Document::ExportPluginInfo> Document::exportPluginInfos() const
{
    std::vector<ExportPluginInfo> infos;
    if (!m_pluginManager)
        return infos;

    const auto pluginInfos = m_pluginManager->pluginInfos();
    infos.reserve(pluginInfos.size());
    for (const auto &info : pluginInfos) {
        ExportPluginInfo out;
        out.id = info.id;
        out.name = info.name;
        out.extensions = info.saveExtensions;
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

QStringList Document::exportSupportedExtensions() const
{
    if (!m_pluginManager)
        return {};
    return m_pluginManager->savableExtensions();
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

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Open Mesh"));

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
    const CallbackMode previousCallbackMode = m_callbackMode;
    m_callbackMode = CallbackMode::Load;
    m_cancelRequested.store(false, std::memory_order_relaxed);
    g_callbackDocument = this;
    int loadMask = 0;
    int err = plugin->load(filename, entry->mesh, logCallback(), &loadMask);
    g_callbackDocument = previousCallbackDocument;
    m_callbackMode = previousCallbackMode;
    const qint64 importElapsedMs = loadTimer.elapsed();

    if (err != 0) {
        emit loadProgressFinished(false, plugin->errorString(err));
        writeLog(tr("Load failed in %1 ms: %2")
            .arg(importElapsedMs)
            .arg(plugin->errorString(err)),
            LogSource::Application);
        if (ownUndoStep)
            endUndoStep(false);
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
    entry->renderTransform.setToIdentity();
    entry->name = QFileInfo(filename).fileName();
    entry->sourcePath = filename;

    QStringList declaredTextureNames;
    QStringList resolvedTexturePaths;
    std::vector<std::string> normalizedMeshTexturePaths;
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
        normalizedMeshTexturePaths.push_back(QDir::toNativeSeparators(resolvedTexturePath).toStdString());
        if (selectedTextureName.isEmpty()) {
            selectedTextureName = textureName;
        }
        if (!selectedExistingTexture && QFileInfo::exists(resolvedTexturePath)) {
            selectedTextureName = textureName;
            selectedExistingTexture = true;
        }
    }
    if (!normalizedMeshTexturePaths.empty())
        entry->mesh.textures = std::move(normalizedMeshTexturePaths);

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
    if (ownUndoStep)
        endUndoStep(true);
    return 0;
}

int Document::reloadMesh(int index)
{
    if (index < 0 || index >= meshCount()) {
        writeLog(tr("Cannot reload: invalid mesh index %1").arg(index), LogSource::Application);
        return -1;
    }

    MeshEntry &entry = mesh(index);
    const QString sourcePath = entry.sourcePath.trimmed();
    if (sourcePath.isEmpty()) {
        writeLog(
            tr("Cannot reload mesh '%1': missing source file path").arg(entry.name),
            LogSource::Application);
        return -2;
    }

    const MeshIOPlugin *plugin = m_pluginManager->pluginFor(sourcePath);
    if (!plugin) {
        writeLog(
            tr("Cannot reload mesh '%1': no plugin found for %2").arg(entry.name, sourcePath),
            LogSource::Application);
        return -3;
    }

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Reload Mesh"));

    const QString oldName = entry.name;
    writeLog(
        tr("Reloading mesh '%1' from %2").arg(oldName, sourcePath),
        LogSource::Application);

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
    emit loadProgressStarted(sourcePath);
    emit loadProgressUpdated(0, tr("Reloading %1").arg(QFileInfo(sourcePath).fileName()));

    VCGMesh reloadedMesh;
    Document *previousCallbackDocument = g_callbackDocument;
    const CallbackMode previousCallbackMode = m_callbackMode;
    m_callbackMode = CallbackMode::Load;
    m_cancelRequested.store(false, std::memory_order_relaxed);
    g_callbackDocument = this;
    int loadMask = 0;
    const int err = plugin->load(sourcePath, reloadedMesh, logCallback(), &loadMask);
    g_callbackDocument = previousCallbackDocument;
    m_callbackMode = previousCallbackMode;
    const qint64 importElapsedMs = loadTimer.elapsed();

    if (err != 0) {
        emit loadProgressFinished(false, plugin->errorString(err));
        writeLog(
            tr("Reload failed in %1 ms: %2")
                .arg(importElapsedMs)
                .arg(plugin->errorString(err)),
            LogSource::Application);
        if (ownUndoStep)
            endUndoStep(false);
        return err;
    }

    vcg::tri::UpdateBounding<VCGMesh>::Box(reloadedMesh);
    const bool hasImportedVertexNormals = (loadMask & vcg::tri::io::Mask::IOM_VERTNORMAL) != 0;
    if (!hasImportedVertexNormals && reloadedMesh.FN() > 0) {
        // Preserve imported vertex normals exactly as provided by the file.
        // Generate smooth normals only when they are missing.
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(reloadedMesh);
    }

    QStringList declaredTextureNames;
    QStringList resolvedTexturePaths;
    QStringList textureFileNames;
    QStringList textureFilePaths;
    std::vector<std::string> normalizedMeshTexturePaths;
    QString selectedTextureName;
    bool selectedExistingTexture = false;
    for (const std::string &rawTextureName : reloadedMesh.textures) {
        const QString textureName = QString::fromStdString(rawTextureName).trimmed();
        if (textureName.isEmpty())
            continue;
        declaredTextureNames.append(textureName);
        const QString resolvedTexturePath = resolveTexturePath(sourcePath, textureName);
        resolvedTexturePaths.append(resolvedTexturePath);
        textureFileNames.append(textureName);
        textureFilePaths.append(resolvedTexturePath);
        normalizedMeshTexturePaths.push_back(QDir::toNativeSeparators(resolvedTexturePath).toStdString());
        if (selectedTextureName.isEmpty())
            selectedTextureName = textureName;
        if (!selectedExistingTexture && QFileInfo::exists(resolvedTexturePath)) {
            selectedTextureName = textureName;
            selectedExistingTexture = true;
        }
    }
    if (!normalizedMeshTexturePaths.empty())
        reloadedMesh.textures = std::move(normalizedMeshTexturePaths);

    deepCopyMesh(reloadedMesh, entry.mesh);
    entry.ioMask = loadMask;
    entry.name = QFileInfo(sourcePath).fileName();
    entry.sourcePath = sourcePath;
    entry.textureFileNames = textureFileNames;
    entry.textureFilePaths = textureFilePaths;
    ++entry.geometryRevision;
    ++entry.materialRevision;

    const qint64 elapsedMs = loadTimer.elapsed();
    const qint64 postProcessElapsedMs = std::max<qint64>(0, elapsedMs - importElapsedMs);
    writeLog(tr("Reloaded mesh '%1' in %2 ms (%3 vertices, %4 faces, %5 edges)")
        .arg(entry.name)
        .arg(elapsedMs)
        .arg(entry.mesh.VN())
        .arg(entry.mesh.FN())
        .arg(entry.mesh.EN()),
        LogSource::Application);
    if (elapsedMs >= 250) {
        writeLog(tr("Reload timing '%1': import %2 ms, post %3 ms")
                .arg(entry.name)
                .arg(importElapsedMs)
                .arg(postProcessElapsedMs),
            LogSource::Application);
    }
    if (m_loadCallbackCount > 0) {
        const float processEventsMs = float(m_loadProcessEventsNs / 1000000.0);
        writeLog(tr("Reload callback stats '%1': %2 calls, %3 UI updates, %4 event pumps (%5 ms)")
                .arg(entry.name)
                .arg(m_loadCallbackCount)
                .arg(m_loadProgressEmitCount)
                .arg(m_loadProcessEventsCount)
                .arg(QString::number(processEventsMs, 'f', 2)),
            LogSource::Application);
    }
    writeLog(tr("File info for '%1': %2 (mask: 0x%3)")
        .arg(entry.name)
        .arg(summarizeLoadMask(loadMask))
        .arg(QString::number(static_cast<quint32>(loadMask), 16).toUpper()),
        LogSource::Application);
    if (!declaredTextureNames.isEmpty()) {
        int existingTextureFiles = 0;
        for (const QString &path : entry.textureFilePaths) {
            if (QFileInfo::exists(path))
                ++existingTextureFiles;
        }
        writeLog(tr("Texture info for '%1': declared [%2], resolved [%3], selected '%4' (%5/%6 files found)")
            .arg(entry.name)
            .arg(declaredTextureNames.join(QStringLiteral(", ")))
            .arg(resolvedTexturePaths.join(QStringLiteral(", ")))
            .arg(selectedTextureName.isEmpty() ? tr("none") : selectedTextureName)
            .arg(existingTextureFiles)
            .arg(entry.textureFilePaths.size()),
            LogSource::Application);
    }

    emit meshDataChanged(index);
    emit loadProgressUpdated(100, tr("Reloaded %1").arg(entry.name));
    emit loadProgressFinished(true, tr("Reloaded %1").arg(entry.name));
    if (ownUndoStep)
        endUndoStep(true);
    return 0;
}

int Document::saveMesh(int index, const QString &filename, const MeshIOSaveOptions &options)
{
    if (index < 0 || index >= meshCount()) {
        writeLog(tr("Cannot save: invalid mesh index %1").arg(index), LogSource::Application);
        return -1;
    }
    const QString normalizedFilename = filename.trimmed();
    if (normalizedFilename.isEmpty()) {
        writeLog(tr("Cannot save: empty target file path"), LogSource::Application);
        return -2;
    }

    const MeshIOPlugin *plugin = m_pluginManager->pluginForSave(normalizedFilename);
    if (!plugin) {
        writeLog(
            tr("No export plugin found for: %1").arg(normalizedFilename),
            LogSource::Application);
        return -3;
    }

    MeshEntry &entry = mesh(index);
    QElapsedTimer timer;
    timer.start();

    writeLog(
        tr("Saving mesh '%1' to %2")
            .arg(entry.name, normalizedFilename),
        LogSource::Application);

    Document *previousCallbackDocument = g_callbackDocument;
    const CallbackMode previousCallbackMode = m_callbackMode;
    m_callbackMode = CallbackMode::Save;
    m_cancelRequested.store(false, std::memory_order_relaxed);
    g_callbackDocument = this;
    const int err = plugin->save(normalizedFilename, entry.mesh, options, logCallback());
    g_callbackDocument = previousCallbackDocument;
    m_callbackMode = previousCallbackMode;

    const qint64 elapsedMs = timer.elapsed();
    if (err != 0) {
        writeLog(
            tr("Save failed in %1 ms: %2")
                .arg(elapsedMs)
                .arg(plugin->errorString(err)),
            LogSource::Application);
        return err;
    }

    writeLog(
        tr("Saved mesh '%1' to %2 in %3 ms")
            .arg(entry.name, normalizedFilename)
            .arg(elapsedMs),
        LogSource::Application);
    return 0;
}

int Document::saveMesh(int index, const QString &filename)
{
    return saveMesh(index, filename, MeshIOSaveOptions{});
}

int Document::saveCurrentMesh(const QString &filename, const MeshIOSaveOptions &options)
{
    if (m_currentMeshIndex < 0 || m_currentMeshIndex >= meshCount()) {
        writeLog(tr("Cannot save: no current mesh selected"), LogSource::Application);
        return -1;
    }
    return saveMesh(m_currentMeshIndex, filename, options);
}

int Document::saveCurrentMesh(const QString &filename)
{
    return saveCurrentMesh(filename, MeshIOSaveOptions{});
}

void Document::beginUndoStep(const QString &label)
{
    if (m_restoringUndoRedo || m_undoStepActive)
        return;

    m_undoStepActive = true;
    m_undoStepLabel = label.trimmed();
    if (m_undoStepLabel.isEmpty())
        m_undoStepLabel = tr("Edit");
    m_pendingUndoBefore = captureUndoState();
}

void Document::endUndoStep(bool commit, bool restoreOnCancel)
{
    if (!m_undoStepActive)
        return;

    const QString label = m_undoStepLabel;
    std::optional<UndoState> before = std::move(m_pendingUndoBefore);
    m_pendingUndoBefore.reset();
    m_undoStepActive = false;
    m_undoStepLabel.clear();

    if (!before.has_value())
        return;

    if (!commit) {
        if (restoreOnCancel) {
            m_restoringUndoRedo = true;
            restoreUndoState(*before);
            m_restoringUndoRedo = false;
        }
        return;
    }

    pushUndoStep(label, std::move(*before), captureUndoState());
}

bool Document::canUndo() const
{
    return m_undoCursor > 0 && !m_undoSteps.empty();
}

bool Document::canRedo() const
{
    return m_undoCursor >= 0 && m_undoCursor < static_cast<int>(m_undoSteps.size());
}

QString Document::undoText() const
{
    if (!canUndo())
        return {};
    return m_undoSteps[static_cast<size_t>(m_undoCursor - 1)].label;
}

QString Document::redoText() const
{
    if (!canRedo())
        return {};
    return m_undoSteps[static_cast<size_t>(m_undoCursor)].label;
}

QStringList Document::undoHistoryLabels() const
{
    QStringList labels;
    if (m_undoCursor <= 0 || m_undoSteps.empty())
        return labels;

    labels.reserve(m_undoCursor);
    for (int i = m_undoCursor - 1; i >= 0; --i)
        labels.push_back(m_undoSteps[static_cast<size_t>(i)].label);
    return labels;
}

QStringList Document::undoStackLabels() const
{
    QStringList labels;
    labels.reserve(static_cast<int>(m_undoSteps.size()));
    for (const UndoStep &step : m_undoSteps)
        labels.push_back(step.label);
    return labels;
}

bool Document::undo()
{
    if (!canUndo() || m_undoStepActive)
        return false;

    const int stepIndex = m_undoCursor - 1;
    m_restoringUndoRedo = true;
    restoreUndoState(m_undoSteps[static_cast<size_t>(stepIndex)].before);
    m_restoringUndoRedo = false;
    m_undoCursor = stepIndex;
    emitUndoRedoStateChanged();
    return true;
}

bool Document::redo()
{
    if (!canRedo() || m_undoStepActive)
        return false;

    const int stepIndex = m_undoCursor;
    m_restoringUndoRedo = true;
    restoreUndoState(m_undoSteps[static_cast<size_t>(stepIndex)].after);
    m_restoringUndoRedo = false;
    m_undoCursor = stepIndex + 1;
    emitUndoRedoStateChanged();
    return true;
}

void Document::clearUndoHistory()
{
    if (m_undoSteps.empty() && m_undoCursor == 0 && !m_undoStepActive)
        return;

    m_undoSteps.clear();
    m_undoCursor = 0;
    m_undoStepActive = false;
    m_undoStepLabel.clear();
    m_pendingUndoBefore.reset();
    emitUndoRedoStateChanged();
}

void Document::setUndoLimit(int limit)
{
    m_undoLimit = std::max(1, limit);
    const int stepCount = static_cast<int>(m_undoSteps.size());
    if (stepCount <= m_undoLimit)
        return;

    const int dropCount = stepCount - m_undoLimit;
    m_undoSteps.erase(m_undoSteps.begin(), m_undoSteps.begin() + dropCount);
    m_undoCursor = std::max(0, m_undoCursor - dropCount);
    emitUndoRedoStateChanged();
}

Document::UndoState Document::captureUndoState() const
{
    UndoState state;
    state.currentMeshIndex = m_currentMeshIndex;
    state.nextMeshId = m_nextMeshId;
    state.meshes.reserve(m_meshes.size());
    for (const auto &entry : m_meshes) {
        if (!entry)
            continue;
        auto snapshot = std::make_unique<MeshEntry>();
        copyMeshEntryMetadata(*entry, *snapshot);
        deepCopyMesh(entry->mesh, snapshot->mesh);
        state.meshes.push_back(std::move(snapshot));
    }
    return state;
}

void Document::restoreUndoState(const UndoState &state)
{
    for (int i = meshCount() - 1; i >= 0; --i) {
        const std::uint64_t meshId = m_meshes[static_cast<size_t>(i)]->meshId;
        m_meshes.erase(m_meshes.begin() + i);
        purgeMeshGpuResources(meshId);
        emit meshRemoved(i);
    }

    m_meshes.clear();
    m_meshes.reserve(state.meshes.size());
    for (size_t i = 0; i < state.meshes.size(); ++i) {
        const std::unique_ptr<MeshEntry> &snapshot = state.meshes[i];
        if (!snapshot)
            continue;
        auto entry = std::make_unique<MeshEntry>();
        copyMeshEntryMetadata(*snapshot, *entry);
        deepCopyMesh(snapshot->mesh, entry->mesh);
        m_meshes.push_back(std::move(entry));
        emit meshAdded(static_cast<int>(m_meshes.size() - 1));
    }

    // Mesh content can jump arbitrarily across undo/redo, so invalidate all cached
    // GPU resources and let passes rebuild lazily on demand.
    clearAllGpuResources();

    m_nextMeshId = state.nextMeshId;
    const int normalizedCurrent =
        (state.currentMeshIndex >= 0 && state.currentMeshIndex < meshCount())
        ? state.currentMeshIndex
        : -1;
    m_currentMeshIndex = normalizedCurrent;
    emit currentMeshChanged(m_currentMeshIndex);

    for (int i = 0; i < meshCount(); ++i) {
        const MeshEntry &entry = mesh(i);
        if (!entry.visible)
            emit meshVisibilityChanged(i, false);
    }
}

void Document::pushUndoStep(const QString &label, UndoState &&before, UndoState &&after)
{
    if (m_undoCursor < static_cast<int>(m_undoSteps.size())) {
        m_undoSteps.erase(
            m_undoSteps.begin() + m_undoCursor,
            m_undoSteps.end());
    }

    m_undoSteps.push_back(UndoStep{label, std::move(before), std::move(after)});
    m_undoCursor = static_cast<int>(m_undoSteps.size());

    if (static_cast<int>(m_undoSteps.size()) > m_undoLimit) {
        const int dropCount = static_cast<int>(m_undoSteps.size()) - m_undoLimit;
        m_undoSteps.erase(m_undoSteps.begin(), m_undoSteps.begin() + dropCount);
        m_undoCursor = std::max(0, m_undoCursor - dropCount);
    }

    emitUndoRedoStateChanged();
}

void Document::emitUndoRedoStateChanged()
{
    emit undoRedoStateChanged(canUndo(), canRedo(), undoText(), redoText());
}

void Document::removeMesh(int index)
{
    if (index < 0 || index >= meshCount())
        return;

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Remove Mesh"));

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
    if (ownUndoStep)
        endUndoStep(true);
}

int Document::addMesh(const VCGMesh &meshData, const QString &name, int ioMask)
{
    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Add Mesh"));

    auto entry = std::make_unique<MeshEntry>();
    deepCopyMesh(meshData, entry->mesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(entry->mesh);

    entry->meshId = m_nextMeshId++;
    entry->geometryRevision = 1;
    entry->materialRevision = 1;
    entry->renderTransform.setToIdentity();
    entry->ioMask = ioMask;
    entry->sourcePath.clear();
    entry->name = name.trimmed().isEmpty()
        ? tr("Mesh %1").arg(meshCount() + 1)
        : name.trimmed();

    for (const std::string &rawTextureName : entry->mesh.textures) {
        const QString texturePath = QString::fromStdString(rawTextureName).trimmed();
        if (texturePath.isEmpty())
            continue;
        entry->textureFilePaths.push_back(texturePath);
        entry->textureFileNames.push_back(QFileInfo(texturePath).fileName());
    }

    const int newIndex = meshCount();
    m_meshes.push_back(std::move(entry));
    writeLog(tr("Added mesh '%1' (%2 vertices, %3 faces, %4 edges)")
                 .arg(this->mesh(newIndex).name)
                 .arg(this->mesh(newIndex).mesh.VN())
                 .arg(this->mesh(newIndex).mesh.FN())
                 .arg(this->mesh(newIndex).mesh.EN()),
        LogSource::Application);
    emit meshAdded(newIndex);
    setCurrentMeshIndex(newIndex);

    if (ownUndoStep)
        endUndoStep(true);
    return newIndex;
}

int Document::duplicateMesh(int sourceIndex, const QString &newName)
{
    if (sourceIndex < 0 || sourceIndex >= meshCount())
        return -1;

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Duplicate Mesh"));

    const MeshEntry &src = mesh(sourceIndex);
    const QString srcName = src.name;
    auto dst = std::make_unique<MeshEntry>();
    copyMeshEntryMetadata(src, *dst);
    deepCopyMesh(src.mesh, dst->mesh);
    dst->meshId = m_nextMeshId++;
    dst->geometryRevision = 1;
    dst->materialRevision = 1;
    dst->sourcePath.clear();
    dst->name = newName.trimmed().isEmpty() ? tr("%1 copy").arg(src.name) : newName.trimmed();

    const int newIndex = meshCount();
    m_meshes.push_back(std::move(dst));
    writeLog(
        tr("Duplicated mesh '%1' as '%2'")
            .arg(srcName)
            .arg(mesh(newIndex).name),
        LogSource::Application);
    emit meshAdded(newIndex);
    setCurrentMeshIndex(newIndex);

    if (ownUndoStep)
        endUndoStep(true);
    return newIndex;
}

QMatrix4x4 Document::meshRenderTransform(int index) const
{
    if (index < 0 || index >= meshCount()) {
        QMatrix4x4 identity;
        identity.setToIdentity();
        return identity;
    }
    return mesh(index).renderTransform;
}

void Document::setMeshRenderTransform(
    int index,
    const QMatrix4x4 &transform,
    const QString &contextMessage)
{
    if (index < 0 || index >= meshCount())
        return;
    MeshEntry &entry = mesh(index);
    if (entry.renderTransform == transform)
        return;

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Modify Mesh Transform"));

    entry.renderTransform = transform;
    if (!contextMessage.trimmed().isEmpty()) {
        writeLog(contextMessage.trimmed(), LogSource::Application);
    } else {
        writeLog(tr("Mesh transform updated: '%1'").arg(entry.name), LogSource::Application);
    }
    emit meshDataChanged(index);

    if (ownUndoStep)
        endUndoStep(true);
}

void Document::setMeshVisible(int index, bool visible)
{
    if (index < 0 || index >= meshCount())
        return;
    MeshEntry &entry = mesh(index);
    if (entry.visible == visible)
        return;
    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Toggle Visibility"));
    entry.visible = visible;
    emit meshVisibilityChanged(index, visible);
    if (ownUndoStep)
        endUndoStep(true);
}

void Document::markMeshGeometryChanged(int index, const QString &contextMessage)
{
    if (index < 0 || index >= meshCount())
        return;
    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Modify Mesh Geometry"));
    MeshEntry &entry = mesh(index);
    ++entry.geometryRevision;
    if (!contextMessage.trimmed().isEmpty()) {
        writeLog(contextMessage.trimmed(), LogSource::Application);
    } else {
        writeLog(tr("Mesh geometry updated: '%1'").arg(entry.name), LogSource::Application);
    }
    emit meshDataChanged(index);
    if (ownUndoStep)
        endUndoStep(true);
}

void Document::markMeshMaterialChanged(int index, const QString &contextMessage)
{
    if (index < 0 || index >= meshCount())
        return;
    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Modify Mesh Material"));
    MeshEntry &entry = mesh(index);
    ++entry.materialRevision;
    if (!contextMessage.trimmed().isEmpty()) {
        writeLog(contextMessage.trimmed(), LogSource::Application);
    } else {
        writeLog(tr("Mesh material updated: '%1'").arg(entry.name), LogSource::Application);
    }
    emit meshDataChanged(index);
    if (ownUndoStep)
        endUndoStep(true);
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
                                      bool needDecoratorBoundaries,
                                      bool qualityFixedRange,
                                      float qualityRangeMin,
                                      float qualityRangeMax,
                                      bool needSelection)
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
    source.qualityFixedRange = qualityFixedRange;
    source.qualityRangeMin = qualityRangeMin;
    source.qualityRangeMax = qualityRangeMax;
    if (source.qualityRangeMin > source.qualityRangeMax)
        std::swap(source.qualityRangeMin, source.qualityRangeMax);
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
        needSelection,
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
        if (stats.rebuiltSelection)
            rebuiltPasses << tr("selection");
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

Document::EdgeFatPassGpuView Document::edgeFatPassGpuView(QRhi *rhi, int meshIndex) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->edgeFatPassView(rhi, meshEntry.meshId);
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

Document::SelectionPassGpuView Document::selectionPassGpuView(QRhi *rhi, int meshIndex) const
{
    if (!m_gpuCache || !rhi || meshIndex < 0 || meshIndex >= meshCount())
        return {};

    const MeshEntry &meshEntry = mesh(meshIndex);
    return m_gpuCache->selectionPassView(rhi, meshEntry.meshId);
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
    const bool isLoadCallback = (m_callbackMode == CallbackMode::Load);
    const bool isFilterCallback = (m_callbackMode == CallbackMode::Filter);
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
        if (isLoadCallback || isFilterCallback) {
            if (isLoadCallback)
                emit loadProgressUpdated(clampedPos, text);
            else
                emit filterProgressUpdated(clampedPos, text);
            ++m_loadProgressEmitCount;
        }

        const bool processEventsThrottleElapsed =
            (m_lastProcessEventsMs < 0) || (nowMs - m_lastProcessEventsMs >= 80);
        if ((isLoadCallback || isFilterCallback) && (forceUiUpdate || processEventsThrottleElapsed)) {
            QElapsedTimer processTimer;
            processTimer.start();
            const QEventLoop::ProcessEventsFlags flags = isFilterCallback
                ? QEventLoop::AllEvents
                : QEventLoop::ExcludeUserInputEvents;
            QCoreApplication::processEvents(flags);
            m_loadProcessEventsNs += processTimer.nsecsElapsed();
            m_lastProcessEventsMs = nowMs;
            ++m_loadProcessEventsCount;
        }
    }

    const int bucket = clampedPos / 10;
    if (bucket == m_lastCallbackBucket)
        return !m_cancelRequested.load(std::memory_order_relaxed);

    m_lastCallbackBucket = bucket;
    decodeText();
    const bool replaceLast = message ? QByteArray(message).endsWith('\r') : false;

    if (text.isEmpty())
        writeLog(tr("Progress %1%").arg(clampedPos), LogSource::VCG, replaceLast);
    else
        writeLog(tr("%1% - %2").arg(clampedPos, 3).arg(text), LogSource::VCG, replaceLast);

    return !m_cancelRequested.load(std::memory_order_relaxed);
}

bool Document::dispatchLogCallback(int pos, const char *message)
{
    if (!g_callbackDocument)
        return true;

    return g_callbackDocument->handleLogCallback(pos, message);
}
