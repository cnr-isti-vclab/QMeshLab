#include "texturefilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include "meshioplugin.h"
#include "pushpull.h"
#include "rastering.h"
#include <vcg/complex/algorithms/point_sampling.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <wrap/io_trimesh/io_mask.h>
#include <QDir>
#include <QFileInfo>
#include <QImage>

namespace {
constexpr QLatin1StringView kFilterSetTexture("set_texture_per_mesh");
constexpr QLatin1StringView kFilterColorToTexture("compute_texmap_from_color");

using Mask = vcg::tri::io::Mask;

MeshFilterRunResult fail(const QString &message)
{
    MeshFilterRunResult r;
    r.success = false;
    r.documentModified = false;
    r.errorMessage = message;
    return r;
}

QString normalizeExistingPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty())
        return canonical;
    return info.absoluteFilePath();
}

QString withSlotSuffix(const QString &basePath, int slotIndex, int slotCount)
{
    if (slotCount <= 1)
        return basePath;

    const QFileInfo info(basePath);
    const QString dir = info.absolutePath();
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();
    const QString fileName = suffix.isEmpty()
        ? QStringLiteral("%1_%2").arg(stem).arg(slotIndex)
        : QStringLiteral("%1_%2.%3").arg(stem).arg(slotIndex).arg(suffix);
    return QDir(dir).filePath(fileName);
}

int ensureTextureSlotIndices(VCGMesh &mesh)
{
    int maxSlot = -1;
    for (auto &face : mesh.face) {
        if (face.IsD())
            continue;
        int faceSlot = face.WT(0).N();
        if (faceSlot < 0)
            faceSlot = 0;
        for (int k = 0; k < 3; ++k) {
            if (face.WT(k).N() < 0)
                face.WT(k).N() = faceSlot;
            maxSlot = std::max(maxSlot, int(face.WT(k).N()));
        }
    }
    return maxSlot + 1;
}

QStringList makeOutputPaths(const QString &requestedPath, int slotCount)
{
    QStringList out;
    out.reserve(slotCount);
    for (int i = 0; i < slotCount; ++i)
        out.push_back(QDir::toNativeSeparators(withSlotSuffix(requestedPath, i, slotCount)));
    return out;
}

QStringList overwriteOutputPaths(const Document::MeshEntry &entry, int slotCount)
{
    QStringList out;
    if (entry.textureFilePaths.size() < slotCount)
        return out;
    out.reserve(slotCount);
    for (int i = 0; i < slotCount; ++i)
        out.push_back(entry.textureFilePaths.at(i));
    return out;
}

bool saveImages(const QStringList &paths, const std::vector<QImage> &images, QString &error)
{
    if (paths.size() != int(images.size())) {
        error = QObject::tr("Texture path/image count mismatch.");
        return false;
    }

    for (int i = 0; i < paths.size(); ++i) {
        const QString path = paths.at(i).trimmed();
        if (path.isEmpty()) {
            error = QObject::tr("Texture output path is empty.");
            return false;
        }
        QFileInfo info(path);
        QDir dir = info.absoluteDir();
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            error = QObject::tr("Cannot create directory '%1'.").arg(dir.path());
            return false;
        }
        if (!images[size_t(i)].save(path)) {
            error = QObject::tr("Failed to save texture '%1'.").arg(path);
            return false;
        }
    }
    return true;
}

void applyTextureAssociation(Document::MeshEntry &entry, const QStringList &paths)
{
    entry.mesh.textures.clear();
    entry.textureFileNames.clear();
    entry.textureFilePaths.clear();
    entry.materialSet.clear();

    for (int i = 0; i < paths.size(); ++i) {
        const QString path = paths.at(i);
        const QFileInfo info(path);
        entry.mesh.textures.push_back(QDir::toNativeSeparators(path).toStdString());
        entry.textureFileNames.push_back(info.fileName());
        entry.textureFilePaths.push_back(QDir::toNativeSeparators(path));

        MeshIOMaterialSlot slot;
        slot.name = QObject::tr("Material %1").arg(i + 1);
        slot.baseColorTexture.fileName = info.fileName();
        slot.baseColorTexture.filePath = QDir::toNativeSeparators(path);
        entry.materialSet.entries.push_back(std::move(slot));
    }
}

} // namespace

QString TextureFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.texture");
}

QString TextureFilterPlugin::name() const
{
    return QObject::tr("Texture Tools");
}

MeshFilterRunResult TextureFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    auto &entry = doc.mesh(meshIndex);
    auto &mesh = entry.mesh;

    if (filterId == QString::fromLatin1(kFilterSetTexture)) {
        if (mesh.VN() <= 0 || mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh must have vertices and faces."));

        const bool hasTexCoords = (entry.ioMask & (Mask::IOM_WEDGTEXCOORD | Mask::IOM_VERTTEXCOORD)) != 0;
        if (!hasTexCoords)
            return fail(QObject::tr("Current mesh does not have texture coordinates."));

        const QString chosenPath = params.getFileOpen(QStringLiteral("textName")).trimmed();
        if (chosenPath.isEmpty())
            return fail(QObject::tr("Texture file not specified."));

        const QString normalizedPath = normalizeExistingPath(chosenPath);
        const QFileInfo info(normalizedPath);
        if (!info.exists() || !info.isFile())
            return fail(QObject::tr("Texture file '%1' does not exist.").arg(chosenPath));

        applyTextureAssociation(entry, { QDir::toNativeSeparators(normalizedPath) });

        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Associated texture '%1' with '%2'.")
                .arg(info.fileName(), entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Set '%1' as the mesh base texture.").arg(info.fileName())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterColorToTexture)) {
        if (mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh must have faces."));
        if ((entry.ioMask & Mask::IOM_VERTCOLOR) == 0)
            return fail(QObject::tr("Current mesh does not have per-vertex color."));
        if ((entry.ioMask & Mask::IOM_WEDGTEXCOORD) == 0)
            return fail(QObject::tr("Current mesh requires per-wedge texture coordinates for this filter."));

        const int textW = params.getInt(QStringLiteral("textW"));
        const int textH = params.getInt(QStringLiteral("textH"));
        const bool overwrite = params.getBool(QStringLiteral("overwrite"));
        const bool pullPush = params.getBool(QStringLiteral("pullpush"));
        const QString requestedPath = params.getFileSave(QStringLiteral("textName")).trimmed();

        if (textW <= 0)
            return fail(QObject::tr("Texture Width has an incorrect value."));
        if (textH <= 0)
            return fail(QObject::tr("Texture Height has an incorrect value."));
        if (!overwrite && requestedPath.isEmpty())
            return fail(QObject::tr("Texture file not specified."));
        if (overwrite && entry.textureFilePaths.isEmpty())
            return fail(QObject::tr("Mesh has no associated texture to overwrite."));

        const int slotCount = std::max(1, ensureTextureSlotIndices(mesh));
        const QStringList outputPaths = overwrite
            ? overwriteOutputPaths(entry, slotCount)
            : makeOutputPaths(requestedPath, slotCount);
        if (outputPaths.size() != slotCount) {
            return fail(QObject::tr("Existing texture association does not match the used texture slots."));
        }

        std::vector<QImage> targetImages;
        targetImages.reserve(size_t(slotCount));
        for (int texIndex = 0; texIndex < slotCount; ++texIndex) {
            QImage img(QSize(textW, textH), QImage::Format_ARGB32);
            img.fill(qRgba(0, 0, 0, 0));
            targetImages.push_back(std::move(img));
        }

        vcg::tri::UpdateTopology<VCGMesh>::FaceFaceFromTexCoord(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);

        RasterSampler sampler(targetImages);
        sampler.InitCallback(doc.progressCallback(), mesh.FN(), 0, 80);
        vcg::tri::SurfaceSampling<VCGMesh, RasterSampler>::Texture(mesh, sampler, textW, textH, true);

        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);

        for (int texIndex = 0; texIndex < slotCount; ++texIndex) {
            for (int y = 0; y < textH; ++y) {
                for (int x = 0; x < textW; ++x) {
                    const QRgb px = targetImages[size_t(texIndex)].pixel(x, y);
                    if (qAlpha(px) < 255 && (!pullPush || qAlpha(px) > 0))
                        targetImages[size_t(texIndex)].setPixel(x, y, px | 0xff000000);
                }
            }
            if (pullPush)
                vcg::PullPush(targetImages[size_t(texIndex)], qRgba(0, 0, 0, 0));
        }

        QString saveError;
        if (!saveImages(outputPaths, targetImages, saveError))
            return fail(saveError);

        applyTextureAssociation(entry, outputPaths);

        doc.markMeshMaterialChanged(
            meshIndex,
            QObject::tr("Baked vertex color to %1 texture image(s) for '%2'.")
                .arg(slotCount)
                .arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Saved %1 texture image(s).").arg(slotCount)
        };
        return result;
    }

    return fail(QObject::tr("Unknown texture filter '%1'.").arg(filterId));
}

void registerTextureFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<TextureFilterPlugin>());
}
