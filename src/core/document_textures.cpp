#include "document_internal.h"

using namespace DocumentInternal;

int Document::meshTextureAssociationCount(const MeshEntry &entry)
{
    int count = std::max(entry.textureFileNames.size(), entry.textureFilePaths.size());
    count = std::max(count, int(entry.mesh.textures.size()));
    count = std::max(count, int(entry.textureAssets.size()));
    count = std::max(count, int(entry.materialSet.entries.size()));
    return count;
}

bool Document::hasMeshTextureAssociation(const MeshEntry &entry)
{
    return meshTextureAssociationCount(entry) > 0;
}

QString Document::meshTextureDisplayName(const MeshEntry &entry, int textureIndex)
{
    if (textureIndex < 0)
        return QString();
    if (textureIndex < int(entry.textureAssets.size())) {
        const MeshIOTextureAsset &asset = entry.textureAssets[size_t(textureIndex)];
        if (!asset.name.trimmed().isEmpty())
            return asset.name.trimmed();
        if (!asset.sourcePath.trimmed().isEmpty())
            return QFileInfo(asset.sourcePath).fileName().trimmed();
    }
    if (textureIndex < entry.textureFileNames.size()) {
        const QString name = entry.textureFileNames.at(textureIndex).trimmed();
        if (!name.isEmpty())
            return name;
    }
    if (textureIndex < entry.textureFilePaths.size()) {
        const QString path = entry.textureFilePaths.at(textureIndex).trimmed();
        if (!path.isEmpty())
            return QFileInfo(path).fileName().trimmed();
    }
    if (textureIndex < int(entry.mesh.textures.size())) {
        const QString path = QString::fromStdString(entry.mesh.textures[size_t(textureIndex)]).trimmed();
        if (!path.isEmpty())
            return QFileInfo(path).fileName().trimmed();
    }
    return QObject::tr("Texture %1").arg(textureIndex + 1);
}

QString Document::meshTextureSourcePath(const MeshEntry &entry, int textureIndex)
{
    if (textureIndex < 0)
        return QString();
    if (textureIndex < int(entry.textureAssets.size())) {
        const QString path = entry.textureAssets[size_t(textureIndex)].sourcePath.trimmed();
        if (!path.isEmpty())
            return QDir::toNativeSeparators(path);
    }
    if (textureIndex < entry.textureFilePaths.size()) {
        const QString path = entry.textureFilePaths.at(textureIndex).trimmed();
        if (!path.isEmpty())
            return QDir::toNativeSeparators(path);
    }
    if (textureIndex < int(entry.mesh.textures.size())) {
        const QString path = QString::fromStdString(entry.mesh.textures[size_t(textureIndex)]).trimmed();
        if (!path.isEmpty())
            return QDir::toNativeSeparators(path);
    }
    return QString();
}

const MeshIOTextureAsset *Document::meshTextureAsset(const MeshEntry &entry, int textureIndex)
{
    if (textureIndex >= 0 && textureIndex < int(entry.materialSet.entries.size())) {
        const int assetIndex = entry.materialSet.entries[size_t(textureIndex)]
                                   .baseColorTexture.assetIndex;
        if (assetIndex >= 0 && assetIndex < int(entry.textureAssets.size()))
            return &entry.textureAssets[size_t(assetIndex)];
        return nullptr;
    }
    if (textureIndex < 0 || textureIndex >= int(entry.textureAssets.size()))
        return nullptr;
    return &entry.textureAssets[size_t(textureIndex)];
}

QString Document::rasterPlaneDisplayName(const RasterPlane &plane, int planeIndex)
{
    return rasterPlaneFallbackName(plane, planeIndex);
}

QString Document::rasterPlaneSourcePath(const RasterPlane &plane)
{
    return QDir::toNativeSeparators(plane.sourcePath.trimmed());
}
