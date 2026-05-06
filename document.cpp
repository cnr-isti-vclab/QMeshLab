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
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <set>

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

MeshIOMaterialTextureRef normalizeMaterialTextureRef(
    const QString &meshFilePath,
    const MeshIOMaterialTextureRef &src)
{
    MeshIOMaterialTextureRef dst = src;
    const QString name = src.fileName.trimmed();
    const QString path = src.filePath.trimmed();
    const QString source = !path.isEmpty() ? path : name;
    if (!source.isEmpty()) {
        if (meshFilePath.trimmed().isEmpty())
            dst.filePath = QDir::fromNativeSeparators(source);
        else
            dst.filePath = resolveTexturePath(meshFilePath, source);
        if (dst.fileName.trimmed().isEmpty())
            dst.fileName = QFileInfo(source).fileName();
    } else {
        dst.filePath.clear();
        if (dst.fileName.trimmed().isEmpty())
            dst.fileName.clear();
    }
    return dst;
}

MeshIOMaterialSet normalizeMaterialSet(
    const QString &meshFilePath,
    const MeshIOMaterialSet &src,
    const VCGMesh &mesh)
{
    MeshIOMaterialSet dst;
    dst.entries.reserve(src.entries.size());
    for (size_t i = 0; i < src.entries.size(); ++i) {
        MeshIOMaterialSlot slot = src.entries[i];
        if (slot.name.trimmed().isEmpty())
            slot.name = QObject::tr("Material %1").arg(i + 1);
        slot.baseColorTexture = normalizeMaterialTextureRef(meshFilePath, slot.baseColorTexture);
        slot.normalTexture = normalizeMaterialTextureRef(meshFilePath, slot.normalTexture);
        slot.occlusionTexture = normalizeMaterialTextureRef(meshFilePath, slot.occlusionTexture);
        slot.roughnessTexture = normalizeMaterialTextureRef(meshFilePath, slot.roughnessTexture);
        dst.entries.push_back(std::move(slot));
    }

    if (dst.entries.empty() && !mesh.textures.empty()) {
        dst.entries.reserve(mesh.textures.size());
        for (size_t i = 0; i < mesh.textures.size(); ++i) {
            const QString texName = QString::fromStdString(mesh.textures[i]).trimmed();
            if (texName.isEmpty())
                continue;
            MeshIOMaterialSlot slot;
            slot.name = QObject::tr("Material %1").arg(i + 1);
            slot.baseColorTexture.fileName = QFileInfo(texName).fileName();
            slot.baseColorTexture.filePath = resolveTexturePath(meshFilePath, texName);
            dst.entries.push_back(std::move(slot));
        }
    }

    return dst;
}

void copyMeshEntryMetadata(const Document::MeshEntry &src, Document::MeshEntry &dst)
{
    dst.meshId = src.meshId;
    dst.geometryRevision = src.geometryRevision;
    dst.materialRevision = src.materialRevision;
    dst.transform = src.transform;
    dst.name = src.name;
    dst.sourcePath = src.sourcePath;
    dst.textureFileNames = src.textureFileNames;
    dst.textureFilePaths = src.textureFilePaths;
    dst.materialSet = src.materialSet;
    dst.visible = src.visible;
    dst.ioMask = src.ioMask;
}

void deepCopyMesh(const VCGMesh &src, VCGMesh &dst)
{
    dst.Clear();

    // Enable storable OCF fields in dst to match src.
    // Ancillary fields (FFAdj, VFAdj, Mark) are never copied — they are
    // transient and must be re-computed after use.
    const bool copyVertTexCoord   = src.vert.IsTexCoordEnabled();
    const bool copyVertCurvDir    = src.vert.IsCurvatureDirEnabled();
    const bool copyFaceWedgeTex   = src.face.IsWedgeTexCoordEnabled();
    if (copyVertTexCoord)  dst.vert.EnableTexCoord();
    if (copyVertCurvDir)   dst.vert.EnableCurvatureDir();
    if (copyFaceWedgeTex)  dst.face.EnableWedgeTexCoord();

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
            dv.C() = sv.cC();
            dv.Q() = sv.cQ();
            dv.Flags() = sv.Flags();
            if (copyVertTexCoord) dv.T()   = sv.cT();
            if (copyVertCurvDir)  { dv.PD1() = sv.cPD1(); dv.PD2() = sv.cPD2();
                                    dv.K1()  = sv.cK1();  dv.K2()  = sv.cK2(); }
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
                if (copyFaceWedgeTex) df.WT(k) = sf.cWT(k);
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

int Document::meshTextureAssociationCount(const MeshEntry &entry)
{
    int count = std::max(entry.textureFileNames.size(), entry.textureFilePaths.size());
    count = std::max(count, int(entry.mesh.textures.size()));
    count = std::max(count, int(entry.materialSet.entries.size()));
    return count;
}

bool Document::hasMeshTextureAssociation(const MeshEntry &entry)
{
    return meshTextureAssociationCount(entry) > 0;
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
    MeshIOMaterialSet loadedMaterialSet;
    // Pre-enable storable OCF fields so the importer can write into them.
    entry->mesh.vert.EnableTexCoord();
    entry->mesh.face.EnableWedgeTexCoord();
    int err = plugin->load(filename, entry->mesh, logCallback(), &loadMask, &loadedMaterialSet);
    // Disable storable OCF fields that the importer did not actually populate.
    if (!(loadMask & vcg::tri::io::Mask::IOM_VERTTEXCOORD))  entry->mesh.vert.DisableTexCoord();
    if (!(loadMask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD))  entry->mesh.face.DisableWedgeTexCoord();
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
    entry->transform.setToIdentity();
    entry->name = QFileInfo(filename).fileName();
    entry->sourcePath = filename;
    entry->materialSet = normalizeMaterialSet(filename, loadedMaterialSet, entry->mesh);

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

    // Also register any PBR channel textures from materialSet that were not listed in
    // mesh.textures (e.g. glTF normal / occlusion / roughness maps that have no UV slot).
    for (const MeshIOMaterialSlot &slot : entry->materialSet.entries) {
        auto addIfNew = [&](const MeshIOMaterialTextureRef &ref) {
            const QString p = ref.filePath.trimmed();
            if (p.isEmpty() || entry->textureFilePaths.contains(p))
                return;
            entry->textureFileNames.append(QFileInfo(p).fileName());
            entry->textureFilePaths.append(p);
        };
        addIfNew(slot.baseColorTexture);
        addIfNew(slot.normalTexture);
        addIfNew(slot.occlusionTexture);
        addIfNew(slot.roughnessTexture);
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
    if (!meshEntry.materialSet.empty()) {
        int baseCount = 0;
        int normalCount = 0;
        int aoCount = 0;
        int roughnessCount = 0;
        for (const MeshIOMaterialSlot &slot : meshEntry.materialSet.entries) {
            if (slot.baseColorTexture.isValid())
                ++baseCount;
            if (slot.normalTexture.isValid())
                ++normalCount;
            if (slot.occlusionTexture.isValid())
                ++aoCount;
            if (slot.roughnessTexture.isValid())
                ++roughnessCount;
        }
        writeLog(
            tr("Material info for '%1': %2 slot(s), base=%3, normal=%4, ao=%5, roughness=%6")
                .arg(meshEntry.name)
                .arg(meshEntry.materialSet.entries.size())
                .arg(baseCount)
                .arg(normalCount)
                .arg(aoCount)
                .arg(roughnessCount),
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
    MeshIOMaterialSet loadedMaterialSet;
    // Pre-enable storable OCF fields so the importer can write into them.
    reloadedMesh.vert.EnableTexCoord();
    reloadedMesh.face.EnableWedgeTexCoord();
    const int err = plugin->load(sourcePath, reloadedMesh, logCallback(), &loadMask, &loadedMaterialSet);
    // Disable storable OCF fields that the importer did not actually populate.
    if (!(loadMask & vcg::tri::io::Mask::IOM_VERTTEXCOORD))  reloadedMesh.vert.DisableTexCoord();
    if (!(loadMask & vcg::tri::io::Mask::IOM_WEDGTEXCOORD))  reloadedMesh.face.DisableWedgeTexCoord();
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
    entry.materialSet = normalizeMaterialSet(sourcePath, loadedMaterialSet, reloadedMesh);
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
    if (!entry.materialSet.empty()) {
        int baseCount = 0;
        int normalCount = 0;
        int aoCount = 0;
        int roughnessCount = 0;
        for (const MeshIOMaterialSlot &slot : entry.materialSet.entries) {
            if (slot.baseColorTexture.isValid())
                ++baseCount;
            if (slot.normalTexture.isValid())
                ++normalCount;
            if (slot.occlusionTexture.isValid())
                ++aoCount;
            if (slot.roughnessTexture.isValid())
                ++roughnessCount;
        }
        writeLog(
            tr("Material info for '%1': %2 slot(s), base=%3, normal=%4, ao=%5, roughness=%6")
                .arg(entry.name)
                .arg(entry.materialSet.entries.size())
                .arg(baseCount)
                .arg(normalCount)
                .arg(aoCount)
                .arg(roughnessCount),
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
    if (m_undoCurrentNode < 0 || m_undoCurrentNode >= static_cast<int>(m_undoNodes.size()))
        return false;
    return m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].parentId >= 0;
}

bool Document::isRestoringUndoRedo() const
{
    return m_restoringUndoRedo;
}

void Document::setViewStateFunctions(
    std::function<ViewState()> capture,
    std::function<void(const ViewState &, bool)> restore)
{
    m_captureViewState = std::move(capture);
    m_restoreViewState = std::move(restore);
}

bool Document::updateUndoNodeCamera(int nodeId)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return false;
    if (!m_captureViewState)
        return false;
    m_undoNodes[static_cast<size_t>(nodeId)].state.viewState = m_captureViewState();
    return true;
}

bool Document::canRedo() const
{
    if (m_undoCurrentNode < 0 || m_undoCurrentNode >= static_cast<int>(m_undoNodes.size()))
        return false;
    return !m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].children.empty();
}

QString Document::undoText() const
{
    if (!canUndo())
        return {};
    return m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].label;
}

QString Document::redoText() const
{
    if (!canRedo())
        return {};
    const auto &node = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    const int preferred = node.preferredChild >= 0 ? node.preferredChild
                                                   : node.children.front();
    return m_undoNodes[static_cast<size_t>(preferred)].label;
}

// Returns the labels on the path from root → current node (oldest first = index 0).
QStringList Document::undoHistoryLabels() const
{
    QStringList labels;
    if (m_undoCurrentNode < 0)
        return labels;
    // Walk up to root collecting labels.
    int id = m_undoCurrentNode;
    while (id >= 0) {
        const auto &node = m_undoNodes[static_cast<size_t>(id)];
        if (!node.label.isEmpty())
            labels.prepend(node.label);
        id = node.parentId;
    }
    return labels;
}

// Returns all labels on the current linear path (root → current), oldest first.
// Used by the linear jump widget for backward compat.
QStringList Document::undoStackLabels() const
{
    return undoHistoryLabels();
}

int Document::undoCursorPosition() const
{
    // Depth of the current node in the tree = number of committed actions on the
    // current branch path.  0 = at root (nothing committed).
    if (m_undoCurrentNode < 0 || m_undoCurrentNode >= static_cast<int>(m_undoNodes.size()))
        return 0;
    int depth = 0;
    int id = m_undoCurrentNode;
    while (m_undoNodes[static_cast<size_t>(id)].parentId >= 0) {
        ++depth;
        id = m_undoNodes[static_cast<size_t>(id)].parentId;
    }
    return depth;
}

int Document::undoCurrentNodeId() const
{
    return m_undoCurrentNode;
}

std::vector<Document::UndoTreeNodeInfo> Document::undoTreeInfo() const
{
    if (m_undoNodes.empty())
        return {};

    // Compute the set of node ids on the current path (root → m_undoCurrentNode).
    std::set<int> onPath;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            onPath.insert(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }

    // BFS from root to produce a depth-first pre-order list.
    std::vector<UndoTreeNodeInfo> result;
    result.reserve(m_undoNodes.size());
    struct Frame { int id; int depth; };
    std::vector<Frame> stack = {{ 0, 0 }};
    while (!stack.empty()) {
        const auto [id, depth] = stack.back();
        stack.pop_back();
        const auto &node = m_undoNodes[static_cast<size_t>(id)];
        UndoTreeNodeInfo info;
        info.nodeId        = id;
        info.parentId      = node.parentId;
        info.depth         = depth;
        info.isCurrent     = (id == m_undoCurrentNode);
        info.isOnCurrentPath = onPath.count(id) > 0;
        info.lane          = node.lane;
        info.label         = node.label;
        result.push_back(std::move(info));
        // Push children in reverse order so the first child is processed first.
        for (int i = static_cast<int>(node.children.size()) - 1; i >= 0; --i)
            stack.push_back({ node.children[static_cast<size_t>(i)], depth + 1 });
    }
    return result;
}

bool Document::jumpToUndoNode(int nodeId, bool restoreCamera)
{
    if (nodeId < 0 || nodeId >= static_cast<int>(m_undoNodes.size()))
        return false;
    if (nodeId == m_undoCurrentNode) {
        if (m_restoreViewState)
            m_restoreViewState(m_undoNodes[static_cast<size_t>(nodeId)].state.viewState, restoreCamera);
        return true;
    }
    if (m_undoStepActive)
        return false;

    // Walk up from both nodes to find the lowest common ancestor (LCA).
    // Build the path from target to root.
    std::vector<int> targetPath;
    {
        int id = nodeId;
        while (id >= 0) {
            targetPath.push_back(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    // Build ancestor set for current node.
    std::set<int> currentAncestors;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            currentAncestors.insert(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    // The LCA is the first node in targetPath that is also an ancestor of current.
    int lca = -1;
    for (int id : targetPath) {
        if (currentAncestors.count(id)) { lca = id; break; }
    }
    if (lca < 0)
        return false;

    {
        std::vector<int> redoPreview;
        for (int id : targetPath) {
            if (id == lca) break;
            redoPreview.push_back(id);
        }
        std::reverse(redoPreview.begin(), redoPreview.end());
        qDebug() << "[JUMP] target=" << nodeId
                 << "current=" << m_undoCurrentNode
                 << "lca=" << lca
                 << "undos=" << (m_undoCurrentNode != lca ? "yes" : "no")
                 << "redoPath=" << QVector<int>(redoPreview.begin(), redoPreview.end());
    }

    // Suppress per-step signals during the multi-step navigation so that
    // intermediate states don't trigger thumbnail capture (grabFramebuffer)
    // or other GUI refreshes that could interfere with the GPU/render state.
    // Camera is never restored for intermediate steps; only the final redo
    // restores it (controlled by the restoreCamera parameter).
    m_suppressUndoRedoSignals = true;
    m_restoreCamera = false;

    // Undo until we reach the LCA.
    while (m_undoCurrentNode != lca) {
        if (!undo()) {
            m_restoreCamera = true;
            m_suppressUndoRedoSignals = false;
            emitUndoRedoStateChanged();
            return false;
        }
    }
    // Now redo along the path from LCA to target.
    // Collect the path from LCA to target (excluding LCA).
    std::vector<int> redoPath;
    for (int id : targetPath) {
        if (id == lca) break;
        redoPath.push_back(id);
    }
    std::reverse(redoPath.begin(), redoPath.end());
    for (int i = 0; i < static_cast<int>(redoPath.size()); ++i) {
        const int id = redoPath[static_cast<size_t>(i)];
        // Restore camera only on the very last redo step.
        m_restoreCamera = (i == static_cast<int>(redoPath.size()) - 1) ? restoreCamera : false;
        // Set the preferred child so redo() follows the correct branch.
        m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].preferredChild = id;
        if (!redo()) {
            m_restoreCamera = true;
            m_suppressUndoRedoSignals = false;
            emitUndoRedoStateChanged();
            return false;
        }
    }
    // If there were no redo steps (target == lca), the undo steps ran with
    // m_restoreCamera=false so render modes were restored but camera was not.
    // Now explicitly re-apply the view with the correct restoreCamera value.
    if (redoPath.empty() && m_restoreViewState) {
        m_restoreViewState(m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].state.viewState, restoreCamera);
    }

    m_restoreCamera = true;
    m_suppressUndoRedoSignals = false;
    emitUndoRedoStateChanged();
    return m_undoCurrentNode == nodeId;
}

bool Document::undo()
{
    if (!canUndo() || m_undoStepActive)
        return false;

    const auto &node = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    qDebug() << "[UNDO]  from=" << m_undoCurrentNode << '(' << node.label << ')'
             << "to=" << node.parentId
             << '(' << m_undoNodes[static_cast<size_t>(node.parentId)].label << ')';
    m_restoringUndoRedo = true;
    restoreUndoState(m_undoNodes[static_cast<size_t>(node.parentId)].state);
    m_restoringUndoRedo = false;
    // Remember which child to return to on redo.
    m_undoNodes[static_cast<size_t>(node.parentId)].preferredChild = m_undoCurrentNode;
    m_undoCurrentNode = node.parentId;
    emitUndoRedoStateChanged();
    return true;
}

bool Document::redo()
{
    if (!canRedo() || m_undoStepActive)
        return false;

    auto &node = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    const int childId = node.preferredChild >= 0 ? node.preferredChild
                                                  : node.children.front();
    qDebug() << "[REDO]  from=" << m_undoCurrentNode << '(' << node.label << ')'
             << "to=" << childId
             << '(' << m_undoNodes[static_cast<size_t>(childId)].label << ')'
             << "prefChild=" << node.preferredChild
             << "children=" << QVector<int>(node.children.begin(), node.children.end());
    m_restoringUndoRedo = true;
    restoreUndoState(m_undoNodes[static_cast<size_t>(childId)].state);
    m_restoringUndoRedo = false;
    m_undoCurrentNode = childId;
    emitUndoRedoStateChanged();
    return true;
}

void Document::clearUndoHistory()
{
    if (m_undoNodes.empty() && m_undoCurrentNode < 0 && !m_undoStepActive)
        return;

    m_undoNodes.clear();
    m_undoGeometryCache.clear();
    m_undoCurrentNode = -1;
    m_undoStepActive = false;
    m_undoStepLabel.clear();
    m_pendingUndoBefore.reset();
    emitUndoRedoStateChanged();
}

void Document::setUndoLimit(int limit)
{
    m_undoLimit = std::max(1, limit);
    pruneUndoTreeToLimit();
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

        UndoState::MeshSnapshot snap;
        // Copy all cheap metadata by value; this is O(1) for numeric/bool fields
        // and O(n_textures) for the string lists — negligible compared to geometry.
        snap.meshId             = entry->meshId;
        snap.geometryRevision   = entry->geometryRevision;
        snap.materialRevision   = entry->materialRevision;
        snap.transform    = entry->transform;
        snap.name               = entry->name;
        snap.sourcePath         = entry->sourcePath;
        snap.textureFileNames   = entry->textureFileNames;
        snap.textureFilePaths   = entry->textureFilePaths;
        snap.materialSet        = entry->materialSet;
        snap.visible            = entry->visible;
        snap.ioMask             = entry->ioMask;

        // Attempt to reuse an already-interned geometry object.
        // Key: (meshId, geometryRevision). As long as the revision hasn't changed
        // since the last capture, every subsequent checkpoint shares the same
        // VCGMesh allocation — zero extra deep-copy cost for non-geometry actions.
        const auto key = std::make_pair(entry->meshId, entry->geometryRevision);
        auto it = m_undoGeometryCache.find(key);
        if (it != m_undoGeometryCache.end())
            snap.geometry = it->second.lock(); // null if all checkpoints were evicted

        if (!snap.geometry) {
            // Cache miss (first capture after a geometry change, or after the cached
            // weak_ptr expired). Deep-copy now and intern for future captures.
            auto g = std::make_shared<VCGMesh>();
            deepCopyMesh(entry->mesh, *g);
            snap.geometry = g;
            m_undoGeometryCache[key] = g; // weak_ptr — does not keep g alive on its own
        }

        state.meshes.push_back(std::move(snap));
    }
    if (m_captureViewState)
        state.viewState = m_captureViewState();
    return state;
}

void Document::restoreUndoState(const UndoState &state)
{
    {
        qDebug() << "[STATE RESTORE] current=" << m_undoCurrentNode
                 << " total nodes=" << static_cast<int>(m_undoNodes.size());
        for (int i = 0; i < static_cast<int>(m_undoNodes.size()); ++i) {
            const auto &n = m_undoNodes[static_cast<size_t>(i)];
            qDebug() << "  node" << i
                     << "lane=" << n.lane
                     << "parent=" << n.parentId
                     << "prefChild=" << n.preferredChild
                     << "children=" << QVector<int>(n.children.begin(), n.children.end())
                     << (i == m_undoCurrentNode ? "<-- current" : "")
                     << "label=" << (n.label.isEmpty() ? QStringLiteral("(root)") : n.label);
        }
    }

    // Evict geometry cache entries whose revision is strictly greater than the
    // revision we are about to restore.  After restoring mesh M to revision R,
    // any subsequent operation will increment R to R+1.  Without eviction,
    // captureUndoState would find the stale (meshId, R+1) entry left by a
    // previous branch and return the wrong geometry for the new node.
    for (const auto &snap : state.meshes) {
        auto it = m_undoGeometryCache.lower_bound(
            std::make_pair(snap.meshId, snap.geometryRevision + 1));
        while (it != m_undoGeometryCache.end() && it->first.first == snap.meshId)
            it = m_undoGeometryCache.erase(it);
    }

    for (int i = meshCount() - 1; i >= 0; --i) {
        const std::uint64_t meshId = m_meshes[static_cast<size_t>(i)]->meshId;
        m_meshes.erase(m_meshes.begin() + i);
        purgeMeshGpuResources(meshId);
        emit meshRemoved(i);
    }

    m_meshes.clear();
    m_meshes.reserve(state.meshes.size());
    for (const auto &snap : state.meshes) {
        auto entry = std::make_unique<MeshEntry>();
        // Restore cheap metadata.
        entry->meshId           = snap.meshId;
        entry->geometryRevision = snap.geometryRevision;
        entry->materialRevision = snap.materialRevision;
        entry->transform  = snap.transform;
        entry->name             = snap.name;
        entry->sourcePath       = snap.sourcePath;
        entry->textureFileNames = snap.textureFileNames;
        entry->textureFilePaths = snap.textureFilePaths;
        entry->materialSet      = snap.materialSet;
        entry->visible          = snap.visible;
        entry->ioMask           = snap.ioMask;
        // The live MeshEntry needs its own mutable copy of the geometry so that
        // subsequent operations (filters, transforms) can modify it freely without
        // corrupting the shared undo snapshot.
        deepCopyMesh(*snap.geometry, entry->mesh);
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
    if (m_restoreViewState)
        m_restoreViewState(state.viewState, m_restoreCamera);
}

void Document::pushUndoStep(const QString &label, UndoState &&before, UndoState &&after)
{
    // If there is no tree yet, create the root node from the "before" state.
    if (m_undoCurrentNode < 0) {
        UndoNode root;
        root.state   = std::move(before);
        root.label   = {};
        root.parentId = -1;
        m_undoNodes.push_back(std::move(root));
        m_undoCurrentNode = 0;
    } else {
        // Update the current node's state to be the "before" snapshot.
        // (It should already match, but this keeps things consistent if the
        //  caller uses an older capture.)
        m_undoNodes[static_cast<size_t>(m_undoCurrentNode)].state = std::move(before);
    }

    // Append a new child node carrying the "after" state.
    const int newId = static_cast<int>(m_undoNodes.size());
    UndoNode child;
    child.state    = std::move(after);
    child.label    = label;
    child.parentId = m_undoCurrentNode;

    // Lane: inherit parent's lane if this is the first child; otherwise open a
    // new lane (max lane currently in tree + 1).
    {
        const auto &parentNode = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
        if (parentNode.children.empty()) {
            child.lane = parentNode.lane;
        } else {
            int maxLane = 0;
            for (const auto &n : m_undoNodes)
                maxLane = std::max(maxLane, n.lane);
            child.lane = maxLane + 1;
        }
    }

    m_undoNodes.push_back(std::move(child));

    // Link parent → new child and make it the preferred redo target.
    auto &parent = m_undoNodes[static_cast<size_t>(m_undoCurrentNode)];
    parent.children.push_back(newId);
    parent.preferredChild = newId;

    m_undoCurrentNode = newId;

    {
        const auto &n = m_undoNodes[static_cast<size_t>(newId)];
        qDebug() << "[STATE SAVED]" << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                 << "idx=" << newId
                 << "lane=" << n.lane
                 << "parent=" << n.parentId
                 << "label=" << n.label;
    }

    pruneUndoTreeToLimit();
    emitUndoRedoStateChanged();
}

// Prune the oldest ancestor nodes until the tree has at most m_undoLimit nodes
// (not counting the root which serves as the "no history" sentinel).
// The current path to m_undoCurrentNode is always preserved.
void Document::pruneUndoTreeToLimit()
{
    // Count nodes along the current path (root to current).
    int depth = 0;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            ++depth;
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
    }
    if (depth - 1 <= m_undoLimit)  // depth-1 = number of transitions
        return;

    // Find the ancestor of m_undoCurrentNode that sits at depth m_undoLimit from
    // the current tip, i.e. the new root after pruning.
    const int stepsToRemove = depth - 1 - m_undoLimit;
    int newRoot = m_undoCurrentNode;
    for (int i = 0; i < m_undoLimit; ++i)
        newRoot = m_undoNodes[static_cast<size_t>(newRoot)].parentId;
    // newRoot is the node to become the new root; everything above it is discarded.
    // Use a BFS/DFS to mark all reachable node ids from newRoot; drop the rest.
    // We do this by building a remapping of old ids → new compact ids.
    std::vector<int> reachable;
    {
        std::vector<int> stack = { newRoot };
        while (!stack.empty()) {
            const int id = stack.back(); stack.pop_back();
            reachable.push_back(id);
            for (int c : m_undoNodes[static_cast<size_t>(id)].children)
                stack.push_back(c);
        }
    }
    std::sort(reachable.begin(), reachable.end());
    // Build old→new id map.
    std::map<int,int> remap;
    for (int ni = 0; ni < static_cast<int>(reachable.size()); ++ni)
        remap[reachable[ni]] = ni;

    std::vector<UndoNode> pruned;
    pruned.reserve(reachable.size());
    for (int oldId : reachable) {
        UndoNode n = std::move(m_undoNodes[static_cast<size_t>(oldId)]);
        n.parentId = (n.parentId >= 0 && remap.count(n.parentId)) ? remap[n.parentId] : -1;
        std::vector<int> newChildren;
        for (int c : n.children)
            if (remap.count(c))
                newChildren.push_back(remap[c]);
        n.children = std::move(newChildren);
        if (n.preferredChild >= 0 && remap.count(n.preferredChild))
            n.preferredChild = remap[n.preferredChild];
        else
            n.preferredChild = n.children.empty() ? -1 : n.children.back();
        pruned.push_back(std::move(n));
    }
    pruned[0].parentId = -1;  // new root has no parent
    m_undoCurrentNode = remap[m_undoCurrentNode];
    m_undoNodes = std::move(pruned);
    (void)stepsToRemove;
}

void Document::emitUndoRedoStateChanged()
{
    if (m_suppressUndoRedoSignals)
        return;
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
    entry->transform.setToIdentity();
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
    entry->materialSet = normalizeMaterialSet(entry->sourcePath, MeshIOMaterialSet{}, entry->mesh);

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

QMatrix4x4 Document::meshTransform(int index) const
{
    if (index < 0 || index >= meshCount()) {
        QMatrix4x4 identity;
        identity.setToIdentity();
        return identity;
    }
    return mesh(index).transform;
}

void Document::setMeshTransform(
    int index,
    const QMatrix4x4 &transform,
    const QString &contextMessage)
{
    if (index < 0 || index >= meshCount())
        return;
    MeshEntry &entry = mesh(index);
    if (entry.transform == transform)
        return;

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Modify Mesh Transform"));

    entry.transform = transform;
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
    entry.visible = visible;
    emit meshVisibilityChanged(index, visible);
}

void Document::setMeshName(int index, const QString &name)
{
    if (index < 0 || index >= meshCount())
        return;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    MeshEntry &entry = mesh(index);
    if (entry.name == trimmed)
        return;

    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Rename Mesh"));

    const QString oldName = entry.name;
    entry.name = trimmed;
    writeLog(
        tr("Renamed mesh '%1' to '%2'").arg(oldName, entry.name),
        LogSource::Application);
    emit meshDataChanged(index);

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

void Document::markMeshSelectionChanged(int index, const QString &contextMessage)
{
    // Selection flags live in per-vertex/per-face BitFlags captured by deepCopyMesh.
    // Bump geometryRevision so the undo cache stores a fresh copy that includes the
    // new selection — identical to markMeshGeometryChanged but with a distinct label.
    if (index < 0 || index >= meshCount())
        return;
    const bool ownUndoStep = !m_restoringUndoRedo && !m_undoStepActive;
    if (ownUndoStep)
        beginUndoStep(tr("Change Selection"));
    MeshEntry &entry = mesh(index);
    ++entry.geometryRevision;
    if (!contextMessage.trimmed().isEmpty()) {
        writeLog(contextMessage.trimmed(), LogSource::Application);
    } else {
        writeLog(tr("Selection changed on '%1'").arg(entry.name), LogSource::Application);
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
                                      bool needSelection,
                                      bool wireRespectFaux)
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
    source.wireRespectFaux = wireRespectFaux;
    source.mesh = &meshEntry.mesh;
    source.textureFilePaths = &meshEntry.textureFilePaths;
    source.materialSet = &meshEntry.materialSet;

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

// ---------------------------------------------------------------------------
// Memory stats
// ---------------------------------------------------------------------------

namespace {

qint64 vcgMeshCpuBytes(const VCGMesh &mesh)
{
    return qint64(mesh.vert.capacity()) * sizeof(VCGVertex)
         + qint64(mesh.edge.capacity()) * sizeof(VCGEdge)
         + qint64(mesh.face.capacity()) * sizeof(VCGFace);
}

qint64 meshEntryCpuBytes(const Document::MeshEntry &entry)
{
    return vcgMeshCpuBytes(entry.mesh);
}

} // namespace

std::vector<Document::CpuMeshMemoryStats> Document::cpuMeshMemoryStats() const
{
    std::vector<CpuMeshMemoryStats> result;
    result.reserve(m_meshes.size());
    for (int i = 0; i < meshCount(); ++i) {
        const MeshEntry &entry = mesh(i);
        CpuMeshMemoryStats s;
        s.meshId = entry.meshId;
        s.meshIndex = i;
        s.name = entry.name;
        s.vertexCapacity = static_cast<int>(entry.mesh.vert.capacity());
        s.edgeCapacity   = static_cast<int>(entry.mesh.edge.capacity());
        s.faceCapacity   = static_cast<int>(entry.mesh.face.capacity());
        s.vertexBytes    = qint64(entry.mesh.vert.capacity()) * sizeof(VCGVertex);
        s.edgeBytes      = qint64(entry.mesh.edge.capacity()) * sizeof(VCGEdge);
        s.faceBytes      = qint64(entry.mesh.face.capacity()) * sizeof(VCGFace);
        result.push_back(s);
    }
    return result;
}

Document::UndoMemoryStats Document::undoMemoryStats() const
{
    UndoMemoryStats stats;
    // Walk the path root → current, reporting each edge as a step.
    // Build the current path (root first).
    std::vector<int> path;
    {
        int id = m_undoCurrentNode;
        while (id >= 0) {
            path.push_back(id);
            id = m_undoNodes[static_cast<size_t>(id)].parentId;
        }
        std::reverse(path.begin(), path.end());
    }
    // Each step i→i+1 along the path.
    for (int pi = 1; pi < static_cast<int>(path.size()); ++pi) {
        UndoStepMemoryInfo info;
        info.label = m_undoNodes[static_cast<size_t>(path[pi])].label;
        for (const auto &snap : m_undoNodes[static_cast<size_t>(path[pi - 1])].state.meshes)
            info.beforeBytes += vcgMeshCpuBytes(*snap.geometry);
        for (const auto &snap : m_undoNodes[static_cast<size_t>(path[pi])].state.meshes)
            info.afterBytes += vcgMeshCpuBytes(*snap.geometry);
        stats.steps.push_back(info);
    }
    // Total bytes across all nodes (de-duplicating shared geometry).
    std::set<const VCGMesh *> seen;
    for (const auto &node : m_undoNodes) {
        for (const auto &snap : node.state.meshes) {
            if (seen.insert(snap.geometry.get()).second)
                stats.totalBytes += vcgMeshCpuBytes(*snap.geometry);
        }
    }
    return stats;
}

std::vector<MeshGpuResourceCache::GpuMeshMemoryStats> Document::gpuMemoryStats() const
{
    return m_gpuCache->gpuMemoryStats();
}
