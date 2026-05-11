#pragma once

#include "document.h"
#include "meshioplugin.h"
#include <QImage>
#include <QString>
#include <QStringList>
#include <vector>

namespace TextureAssociationUtils {

QString normalizeExistingPath(const QString &path);
QImage makeDummyTexture(int imageSize, int checkSize, bool checkerboard);

MeshIOTextureAsset makeTextureAssetFromPath(const QString &path);
MeshIOTextureAsset makeTextureAssetFromImage(
    const QImage &image,
    const QString &name,
    const QString &sourcePath = {});
std::vector<MeshIOTextureAsset> makeTextureAssetsFromSavedImages(
    const QStringList &paths,
    const std::vector<QImage> &images);

QStringList associatedTexturePaths(const Document::MeshEntry &entry);
bool loadAssociatedTextureImage(
    const Document::MeshEntry &entry,
    int textureIndex,
    QImage &image,
    QString &error);
bool saveImages(const QStringList &paths, const std::vector<QImage> &images, QString &error);

void rebuildLegacyTextureAssociation(Document::MeshEntry &entry);
void replaceTextureAssociations(
    Document::MeshEntry &entry,
    const std::vector<MeshIOTextureAsset> &assets);
void appendTextureAssociations(
    Document::MeshEntry &entry,
    const std::vector<MeshIOTextureAsset> &assets);
void ensureMaterialSlotCount(Document::MeshEntry &entry, int count);
int ensureTextureListed(Document::MeshEntry &entry, const QString &path);

} // namespace TextureAssociationUtils
