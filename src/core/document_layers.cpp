#include "document_internal.h"

using namespace DocumentInternal;

void Document::removeMesh(int index)
{
    if (index < 0 || index >= meshCount())
        return;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
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
    setCurrentMeshIndexInternal(newCurrent, m_currentLayerKind == CurrentLayerKind::Mesh);
    if (ownUndoStep)
        endUndoStep(true);
}

int Document::addMesh(const VCGMesh &meshData, const QString &name, int ioMask)
{
    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Add Mesh"));

    auto entry = std::make_unique<MeshEntry>();
    // deepCopyMesh copies only live elements, so meshes added through the
    // document API enter already compact.
    deepCopyMesh(meshData, entry->mesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(entry->mesh);

    entry->meshId = m_nextMeshId++;
    entry->geometryRevision = m_nextGeometryRevision++;
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
    syncTextureAssetsFromLegacyAssociation(*entry);
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

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Duplicate Mesh"));

    const MeshEntry &src = mesh(sourceIndex);
    const QString srcName = src.name;
    auto dst = std::make_unique<MeshEntry>();
    copyMeshEntryMetadata(src, *dst);
    deepCopyMesh(src.mesh, dst->mesh);
    dst->meshId = m_nextMeshId++;
    dst->geometryRevision = m_nextGeometryRevision++;
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

int Document::loadRasterImage(const QString &filename)
{
    const QString normalizedFilename = filename.trimmed();
    if (normalizedFilename.isEmpty())
        return -1;

    QImageReader reader(normalizedFilename);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        writeLog(
            tr("Failed to load raster image '%1': %2")
                .arg(normalizedFilename, reader.errorString()),
            LogSource::Application);
        return -1;
    }

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep) {
        ScriptAction sa;
        sa.kind = QStringLiteral("load_raster");
        sa.filePaths = QStringList{normalizedFilename};
        beginUndoStep(tr("Open Raster"), sa);
    }

    const int index = addRasterImage(
        image,
        QFileInfo(normalizedFilename).fileName(),
        normalizedFilename);

    if (ownUndoStep)
        endUndoStep(index >= 0);

    return index;
}


int Document::addRaster(const RasterEntry &rasterData)
{
    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Add Raster"));

    auto entry = std::make_unique<RasterEntry>(rasterData);
    normalizeRasterEntry(*entry, rasterCount());
    entry->rasterId = m_nextRasterId++;
    entry->imageRevision = entry->imageRevision == 0 ? 1 : entry->imageRevision;
    entry->cameraRevision = entry->cameraRevision == 0 ? 1 : entry->cameraRevision;

    const int newIndex = rasterCount();
    m_rasters.push_back(std::move(entry));
    if (!m_bulkLoading) {
        emit rasterAdded(newIndex);
        setCurrentRasterIndex(newIndex);
    }

    if (ownUndoStep)
        endUndoStep(true);
    return newIndex;
}

int Document::addRasterImage(
    const QImage &image,
    const QString &name,
    const QString &sourcePath,
    const CameraShot &shot)
{
    if (image.isNull())
        return -1;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Add Raster"));

    RasterPlane plane;
    plane.semantic = RasterPlaneSemantic::RGBA;
    plane.name = name.trimmed().isEmpty()
        ? QFileInfo(sourcePath).fileName()
        : name.trimmed();
    plane.sourcePath = sourcePath.trimmed();
    plane.size = image.size();
    plane.image = image;

    RasterEntry entry;
    entry.name = plane.name;
    entry.sourcePath = sourcePath.trimmed();
    entry.visible = true;
    entry.shot = shot;
    entry.currentPlaneIndex = 0;
    entry.planes.push_back(std::move(plane));

    const int index = addRaster(entry);

    if (ownUndoStep)
        endUndoStep(index >= 0);
    return index;
}

void Document::removeRaster(int index)
{
    if (index < 0 || index >= rasterCount())
        return;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Remove Raster"));

    const QString rasterName = raster(index).name;
    int newCurrent = m_currentRasterIndex;
    if (m_currentRasterIndex == index) {
        if (rasterCount() == 1)
            newCurrent = -1;
        else
            newCurrent = (index < rasterCount() - 1) ? index : (rasterCount() - 2);
    } else if (m_currentRasterIndex > index) {
        newCurrent = m_currentRasterIndex - 1;
    }

    m_rasters.erase(m_rasters.begin() + index);
    writeLog(tr("Removed raster '%1'").arg(rasterName), LogSource::Application);
    emit rasterRemoved(index);
    setCurrentRasterIndexInternal(newCurrent, m_currentLayerKind == CurrentLayerKind::Raster);

    if (ownUndoStep)
        endUndoStep(true);
}

void Document::clearAllLayers()
{
    for (const auto &entry : m_meshes)
        purgeMeshGpuResources(entry->meshId);
    m_meshes.clear();
    m_rasters.clear();
    m_currentMeshIndex = -1;
    m_currentRasterIndex = -1;
    m_currentLayerKind = CurrentLayerKind::None;
    emit meshRemoved(-1);
    emit rasterRemoved(-1);
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

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
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

void Document::setRasterVisible(int index, bool visible)
{
    if (index < 0 || index >= rasterCount())
        return;
    RasterEntry &entry = raster(index);
    if (entry.visible == visible)
        return;
    entry.visible = visible;
    emit rasterVisibilityChanged(index, visible);
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

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
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

void Document::setRasterName(int index, const QString &name)
{
    if (index < 0 || index >= rasterCount())
        return;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    RasterEntry &entry = raster(index);
    if (entry.name == trimmed)
        return;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Rename Raster"));

    const QString oldName = entry.name;
    entry.name = trimmed;
    writeLog(
        tr("Renamed raster '%1' to '%2'").arg(oldName, entry.name),
        LogSource::Application);
    emit rasterDataChanged(index);

    if (ownUndoStep)
        endUndoStep(true);
}

void Document::setRasterShot(int index, const CameraShot &shot, const QString &contextMessage)
{
    if (index < 0 || index >= rasterCount())
        return;

    RasterEntry &entry = raster(index);
    if (entry.shot.toVcgShot() == shot.toVcgShot())
        return;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Modify Raster Camera"));

    entry.shot = shot;
    ++entry.cameraRevision;
    if (!contextMessage.trimmed().isEmpty()) {
        writeLog(contextMessage.trimmed(), LogSource::Application);
    } else {
        writeLog(tr("Raster camera updated: '%1'").arg(entry.name), LogSource::Application);
    }
    emit rasterDataChanged(index);

    if (ownUndoStep)
        endUndoStep(true);
}

void Document::setCurrentRasterIndex(int index)
{
    setCurrentRasterIndexInternal(index, true);
}

void Document::setCurrentRasterPlaneIndex(int rasterIndex, int planeIndex)
{
    if (rasterIndex < 0 || rasterIndex >= rasterCount())
        return;
    RasterEntry &entry = raster(rasterIndex);
    if (planeIndex < 0 || planeIndex >= int(entry.planes.size()))
        return;
    if (entry.currentPlaneIndex == planeIndex)
        return;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Select Raster Plane"));

    entry.currentPlaneIndex = planeIndex;
    writeLog(
        tr("Raster '%1': selected plane %2").arg(entry.name).arg(planeIndex),
        LogSource::Application);
    emit rasterDataChanged(rasterIndex);

    if (ownUndoStep)
        endUndoStep(true);
}

void Document::setCurrentRasterIndexInternal(int index, bool makeCurrentLayer)
{
    const int normalizedIndex = (index >= 0 && index < rasterCount()) ? index : -1;
    const CurrentLayerKind nextLayerKind =
        (normalizedIndex >= 0) ? CurrentLayerKind::Raster : CurrentLayerKind::None;
    const bool rasterChanged = (m_currentRasterIndex != normalizedIndex);
    const bool layerChanged =
        makeCurrentLayer
        && (m_currentLayerKind != nextLayerKind
            || (nextLayerKind == CurrentLayerKind::Raster && m_currentRasterIndex != normalizedIndex));
    if (!rasterChanged && !layerChanged)
        return;
    m_currentRasterIndex = normalizedIndex;
    if (makeCurrentLayer)
        m_currentLayerKind = nextLayerKind;
    if (rasterChanged)
        emit currentRasterChanged(m_currentRasterIndex);
    if (layerChanged)
        emit currentLayerChanged(m_currentLayerKind, m_currentRasterIndex);
}

void Document::ensureRasterPlaneImage(RasterPlane &plane)
{
    if (!plane.image.isNull())
        return;
    if (plane.sourcePath.trimmed().isEmpty())
        return;
    QImageReader reader(plane.sourcePath);
    reader.setAutoTransform(true);
    plane.image = reader.read();
    if (!plane.image.isNull())
        plane.size = plane.image.size();
}

void Document::markRasterImageChanged(int index, const QString &contextMessage)
{
    if (index < 0 || index >= rasterCount())
        return;

    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Modify Raster Image"));

    RasterEntry &entry = raster(index);
    ++entry.imageRevision;
    if (!contextMessage.trimmed().isEmpty()) {
        writeLog(contextMessage.trimmed(), LogSource::Application);
    } else {
        writeLog(tr("Raster image updated: '%1'").arg(entry.name), LogSource::Application);
    }
    emit rasterDataChanged(index);

    if (ownUndoStep)
        endUndoStep(true);
}

void Document::markMeshGeometryChanged(int index, const QString &contextMessage)
{
    if (index < 0 || index >= meshCount())
        return;
    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Modify Mesh Geometry"));
    MeshEntry &entry = mesh(index);
    entry.modified = true;
    entry.geometryRevision = m_nextGeometryRevision++;
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
    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Modify Mesh Material"));
    MeshEntry &entry = mesh(index);
    entry.modified = true;
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
    const bool ownUndoStep = !m_undoManager->isRestoring() && !m_undoManager->isStepActive();
    if (ownUndoStep)
        beginUndoStep(tr("Change Selection"));
    MeshEntry &entry = mesh(index);
    entry.modified = true;
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
    setCurrentMeshIndexInternal(index, true);
}

void Document::setCurrentMeshIndexInternal(int index, bool makeCurrentLayer)
{
    const int normalizedIndex = (index >= 0 && index < meshCount()) ? index : -1;
    const CurrentLayerKind nextLayerKind =
        (normalizedIndex >= 0) ? CurrentLayerKind::Mesh : CurrentLayerKind::None;
    const bool meshChanged = (m_currentMeshIndex != normalizedIndex);
    const bool layerChanged =
        makeCurrentLayer
        && (m_currentLayerKind != nextLayerKind
            || (nextLayerKind == CurrentLayerKind::Mesh && m_currentMeshIndex != normalizedIndex));
    if (!meshChanged && !layerChanged)
        return;
    m_currentMeshIndex = normalizedIndex;
    if (makeCurrentLayer)
        m_currentLayerKind = nextLayerKind;
    if (meshChanged)
        emit currentMeshChanged(m_currentMeshIndex);
    if (layerChanged)
        emit currentLayerChanged(m_currentLayerKind, m_currentMeshIndex);
}
