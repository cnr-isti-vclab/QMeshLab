#include "layerfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMatrix4x4>
#include <QVector4D>
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/append.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr QLatin1StringView kSplitFaces("generate_from_selected_faces");
constexpr QLatin1StringView kSplitVertices("generate_from_selected_vertices");
constexpr QLatin1StringView kSplitConnected("generate_splitting_by_connected_components");
constexpr QLatin1StringView kDuplicate("generate_copy_of_current_mesh");
constexpr QLatin1StringView kDeleteCurrent("delete_current_mesh");
constexpr QLatin1StringView kDeleteHidden("delete_non_visible_meshes");
constexpr QLatin1StringView kFlatten("generate_by_merging_visible_meshes");
constexpr QLatin1StringView kRenameMesh("set_mesh_name");
constexpr QLatin1StringView kRenderFromRenderStateJson("render_from_render_state_json");
using Mask = vcg::tri::io::Mask;
using Sel = vcg::tri::UpdateSelection<VCGMesh>;

MeshFilterRunResult fail(const QString &message)
{
    return { false, false, message };
}

MeshFilterRunResult successInfo(bool modified, const QStringList &info = {}, const QVector<int> &newMeshes = {})
{
    MeshFilterRunResult result;
    result.success = true;
    result.documentModified = modified;
    result.infoMessages = info;
    result.newMeshIndices = newMeshes;
    return result;
}

template<typename Scalar>
vcg::Point3<Scalar> qMatrixMapPoint(const QMatrix4x4 &matrix, const vcg::Point3<Scalar> &point)
{
    const QVector4D mapped = matrix * QVector4D(point.X(), point.Y(), point.Z(), 1.0f);
    return vcg::Point3<Scalar>(Scalar(mapped.x()), Scalar(mapped.y()), Scalar(mapped.z()));
}

bool isIdentityTransform(const QMatrix4x4 &matrix, float eps = 1e-6f)
{
    QMatrix4x4 identity;
    identity.setToIdentity();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::abs(matrix(r, c) - identity(r, c)) > eps)
                return false;
    return true;
}

void applyTransformToMesh(VCGMesh &mesh, const QMatrix4x4 &transform)
{
    if (isIdentityTransform(transform))
        return;
    for (VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        v.P() = qMatrixMapPoint<float>(transform, v.cP());
    }
}

void copyMaterialMetadata(const Document::MeshEntry &src, Document::MeshEntry &dst)
{
    dst.textureFileNames = src.textureFileNames;
    dst.textureFilePaths = src.textureFilePaths;
    dst.materialSet = src.materialSet;
    dst.ioMask |= (src.ioMask & (Mask::IOM_WEDGTEXCOORD | Mask::IOM_VERTTEXCOORD | Mask::IOM_VERTCOLOR | Mask::IOM_FACECOLOR | Mask::IOM_VERTQUALITY | Mask::IOM_FACEQUALITY));
}

void rebuildNormalsAndBounds(VCGMesh &mesh)
{
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    if (mesh.FN() > 0)
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

int addDerivedMesh(
    Document &doc,
    const VCGMesh &mesh,
    const QString &name,
    int ioMask,
    const Document::MeshEntry &sourceEntry,
    const QMatrix4x4 &transform)
{
    const int newIndex = doc.addMesh(mesh, name, ioMask);
    if (newIndex < 0)
        return newIndex;
    Document::MeshEntry &newEntry = doc.mesh(newIndex);
    newEntry.transform = transform;
    copyMaterialMetadata(sourceEntry, newEntry);
    return newIndex;
}

std::unique_ptr<VCGMesh> meshCopy(const VCGMesh &mesh)
{
    auto copy = std::make_unique<VCGMesh>();
    vcg::tri::Append<VCGMesh, VCGMesh>::MeshCopyConst(*copy, mesh);
    return copy;
}

int unionIoMask(const Document &doc, const std::vector<int> &indices)
{
    int mask = 0;
    for (int index : indices)
        mask |= doc.mesh(index).ioMask;
    return mask;
}

int findMeshIndexById(const Document &doc, std::uint64_t meshId)
{
    for (int i = 0; i < doc.meshCount(); ++i) {
        if (doc.mesh(i).meshId == meshId)
            return i;
    }
    return -1;
}

QString mergedLayerName()
{
    return QObject::tr("Merged Mesh");
}

bool mergeCameraStateIntoRenderState(
    const QString &cameraStateJson,
    const QString &renderStateJson,
    QString &outMergedRenderState,
    QString &errorMessage)
{
    QJsonParseError renderErr;
    const QJsonDocument renderDoc = QJsonDocument::fromJson(renderStateJson.toUtf8(), &renderErr);
    if (renderDoc.isNull() || !renderDoc.isObject()) {
        errorMessage = QObject::tr("Invalid render-state JSON: %1").arg(renderErr.errorString());
        return false;
    }
    QJsonObject renderRoot = renderDoc.object();

    QJsonParseError cameraErr;
    const QJsonDocument cameraDoc = QJsonDocument::fromJson(cameraStateJson.toUtf8(), &cameraErr);
    if (cameraDoc.isNull() || !cameraDoc.isObject()) {
        errorMessage = QObject::tr("Invalid camera-state JSON: %1").arg(cameraErr.errorString());
        return false;
    }
    const QJsonObject cameraRoot = cameraDoc.object();

    const QJsonObject cameraTrackball = cameraRoot.value(QStringLiteral("trackball")).toObject();
    if (!cameraTrackball.isEmpty()) {
        renderRoot.insert(QStringLiteral("trackball"), cameraTrackball);
    } else {
        renderRoot.insert(QStringLiteral("trackball"), cameraRoot);
    }

    outMergedRenderState = QString::fromUtf8(QJsonDocument(renderRoot).toJson(QJsonDocument::Compact));
    return true;
}

} // namespace

QString LayerFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.layer");
}

QString LayerFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Layer Filters");
}

MeshFilterRunResult LayerFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    if (filterId == QString::fromLatin1(kRenderFromRenderStateJson)) {
        QString renderStateJson = params.getRenderState(QStringLiteral("render_state")).trimmed();
        const QString cameraStateJson = params.getCameraState(QStringLiteral("camera_state")).trimmed();
        if (renderStateJson.isEmpty()) {
            return fail(QObject::tr("No render-state JSON provided."));
        }
        if (cameraStateJson.isEmpty()) {
            return fail(QObject::tr("No camera-state JSON provided."));
        }

        QString mergedRenderStateJson;
        QString mergeError;
        if (!mergeCameraStateIntoRenderState(cameraStateJson, renderStateJson, mergedRenderStateJson, mergeError)) {
            return fail(QObject::tr("Render-state preparation failed: %1").arg(mergeError));
        }

        const int w = std::max(0, params.getInt(QStringLiteral("output_width"), 0));
        const int h = std::max(0, params.getInt(QStringLiteral("output_height"), 0));
        const QSize targetSize = (w > 0 && h > 0) ? QSize(w, h) : QSize();

        QImage snapshot;
        CameraShot shot;
        QString captureError;
        if (!doc.renderSnapshotFromStateJson(mergedRenderStateJson, targetSize, snapshot, shot, &captureError)) {
            return fail(QObject::tr("Render from state JSON failed: %1").arg(captureError));
        }
        if (snapshot.isNull())
            return fail(QObject::tr("Render from state JSON produced an empty image."));

        bool modified = false;
        QStringList info;

        const QString savePath = params.getFileSave(QStringLiteral("save_png_path")).trimmed();
        if (!savePath.isEmpty()) {
            if (!snapshot.save(savePath, "PNG")) {
                return fail(QObject::tr("Failed to save PNG snapshot to '%1'.").arg(savePath));
            }
            info << QObject::tr("Saved PNG snapshot: %1").arg(savePath);
        }

        if (params.getBool(QStringLiteral("add_as_raster"), true)) {
            const QString requestedName = params.getString(QStringLiteral("raster_name")).trimmed();
            const QString rasterName = requestedName.isEmpty()
                ? QObject::tr("Programmatic Render")
                : requestedName;
            const QString rasterSourcePath = savePath.isEmpty() ? QString() : QFileInfo(savePath).absoluteFilePath();
            const int rasterIndex = doc.addRasterImage(snapshot, rasterName, rasterSourcePath, shot);
            if (rasterIndex < 0)
                return fail(QObject::tr("Failed to add rendered snapshot as raster layer."));
            modified = true;
            info << QObject::tr("Added raster layer '%1'.").arg(doc.raster(rasterIndex).name);
        }

        if (savePath.isEmpty() && !params.getBool(QStringLiteral("add_as_raster"), true)) {
            info << QObject::tr("Snapshot rendered successfully (no output target selected).");
        }

        return successInfo(modified, info);
    }

    const int currentIndex = doc.currentMeshIndex();

    if (filterId == QString::fromLatin1(kDeleteHidden)) {
        std::vector<int> toDelete;
        for (int i = 0; i < doc.meshCount(); ++i) {
            if (!doc.mesh(i).visible)
                toDelete.push_back(i);
        }
        if (toDelete.empty())
            return successInfo(false, { QObject::tr("No non-visible mesh layers to delete.") });

        std::sort(toDelete.rbegin(), toDelete.rend());
        for (int idx : toDelete)
            doc.removeMesh(idx);
        return successInfo(true, { QObject::tr("Deleted %1 non-visible mesh layer(s).").arg(toDelete.size()) });
    }

    if (currentIndex < 0 || currentIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    Document::MeshEntry &entry = doc.mesh(currentIndex);

    if (filterId == QString::fromLatin1(kRenameMesh)) {
        const QString newName = params.getString(QStringLiteral("newName")).trimmed();
        if (newName.isEmpty())
            return fail(QObject::tr("New mesh name cannot be empty."));
        if (newName == entry.name)
            return successInfo(false, { QObject::tr("Mesh is already named '%1'.").arg(newName) });
        doc.setMeshName(currentIndex, newName);
        return successInfo(true, { QObject::tr("Renamed current mesh to '%1'.").arg(newName) });
    }

    if (filterId == QString::fromLatin1(kDeleteCurrent)) {
        const QString name = entry.name;
        doc.removeMesh(currentIndex);
        return successInfo(true, { QObject::tr("Deleted mesh '%1'.").arg(name) });
    }

    if (filterId == QString::fromLatin1(kDuplicate)) {
        const int newIndex = doc.duplicateMesh(currentIndex);
        if (newIndex < 0)
            return fail(QObject::tr("Failed to duplicate the current mesh."));
        return successInfo(true,
            { QObject::tr("Duplicated current mesh as '%1'.").arg(doc.mesh(newIndex).name) },
            { newIndex });
    }

    if (filterId == QString::fromLatin1(kSplitVertices)) {
        const int selectedVerts = Sel::VertexCount(entry.mesh);
        if (selectedVerts <= 0)
            return fail(QObject::tr("No selected vertices found on current mesh."));

        auto subsetSource = meshCopy(entry.mesh);
        Sel::FaceClear(*subsetSource);
        VCGMesh subsetMesh;
        vcg::tri::Append<VCGMesh, VCGMesh>::Mesh(subsetMesh, *subsetSource, true);
        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(subsetMesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(subsetMesh);
        const int newIndex = addDerivedMesh(
            doc,
            subsetMesh,
            QObject::tr("SelectedVerticesSubset"),
            entry.ioMask,
            entry,
            entry.transform);
        if (newIndex < 0)
            return fail(QObject::tr("Failed to create the destination layer."));

        const bool deleteOriginal = params.getBool(QStringLiteral("DeleteOriginal"), true);
        if (deleteOriginal) {
            Sel::FaceFromVertexLoose(entry.mesh);
            for (VCGFace &f : entry.mesh.face)
                if (!f.IsD() && f.IsS())
                    vcg::tri::Allocator<VCGMesh>::DeleteFace(entry.mesh, f);
            for (VCGVertex &v : entry.mesh.vert)
                if (!v.IsD() && v.IsS())
                    vcg::tri::Allocator<VCGMesh>::DeleteVertex(entry.mesh, v);
            Sel::VertexClear(entry.mesh);
            Sel::FaceClear(entry.mesh);
            vcg::tri::Allocator<VCGMesh>::CompactEveryVector(entry.mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);
            if (entry.mesh.FN() > 0)
                vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry.mesh);
            doc.markMeshGeometryChanged(currentIndex, QObject::tr("Moved selected vertices from '%1' into a new layer").arg(entry.name));
        }

        return successInfo(true,
            {
                deleteOriginal
                    ? QObject::tr("Moved %1 selected vertices into '%2'.").arg(selectedVerts).arg(doc.mesh(newIndex).name)
                    : QObject::tr("Copied %1 selected vertices into '%2'.").arg(selectedVerts).arg(doc.mesh(newIndex).name)
            },
            { newIndex });
    }

    if (filterId == QString::fromLatin1(kSplitFaces)) {
        const int selectedFaces = Sel::FaceCount(entry.mesh);
        if (selectedFaces <= 0)
            return fail(QObject::tr("No selected faces found on current mesh."));

        auto subsetSource = meshCopy(entry.mesh);
        Sel::VertexFromFaceLoose(*subsetSource);
        const int selectedVerts = Sel::VertexCount(*subsetSource);
        VCGMesh subsetMesh;
        vcg::tri::Append<VCGMesh, VCGMesh>::Mesh(subsetMesh, *subsetSource, true);
        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(subsetMesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(subsetMesh);
        if (subsetMesh.FN() > 0)
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(subsetMesh);
        const int newIndex = addDerivedMesh(
            doc,
            subsetMesh,
            QObject::tr("SelectedFacesSubset"),
            entry.ioMask,
            entry,
            entry.transform);
        if (newIndex < 0)
            return fail(QObject::tr("Failed to create the destination layer."));

        const bool deleteOriginal = params.getBool(QStringLiteral("DeleteOriginal"), true);
        if (deleteOriginal) {
            Sel::VertexClear(entry.mesh);
            Sel::VertexFromFaceStrict(entry.mesh);
            for (VCGFace &f : entry.mesh.face)
                if (!f.IsD() && f.IsS())
                    vcg::tri::Allocator<VCGMesh>::DeleteFace(entry.mesh, f);
            for (VCGVertex &v : entry.mesh.vert)
                if (!v.IsD() && v.IsS())
                    vcg::tri::Allocator<VCGMesh>::DeleteVertex(entry.mesh, v);
            Sel::VertexClear(entry.mesh);
            Sel::FaceClear(entry.mesh);
            vcg::tri::Allocator<VCGMesh>::CompactEveryVector(entry.mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(entry.mesh);
            if (entry.mesh.FN() > 0)
                vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(entry.mesh);
            doc.markMeshGeometryChanged(currentIndex, QObject::tr("Moved selected faces from '%1' into a new layer").arg(entry.name));
        }

        return successInfo(true,
            {
                deleteOriginal
                    ? QObject::tr("Moved %1 faces and %2 vertices into '%3'.").arg(selectedFaces).arg(selectedVerts).arg(doc.mesh(newIndex).name)
                    : QObject::tr("Copied %1 faces and %2 vertices into '%3'.").arg(selectedFaces).arg(selectedVerts).arg(doc.mesh(newIndex).name)
            },
            { newIndex });
    }

    if (filterId == QString::fromLatin1(kSplitConnected)) {
        if (entry.mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh has no faces."));

        // FF adjacency is already built by the framework via inputPrepare.
        std::vector<std::pair<int, VCGFace::FacePointer>> connectedCompVec;
        const int numCC = vcg::tri::Clean<VCGMesh>::ConnectedComponents(entry.mesh, connectedCompVec);
        Sel::FaceClear(entry.mesh);
        Sel::VertexClear(entry.mesh);

        QVector<int> newIndices;
        std::vector<std::uint64_t> newMeshIds;
        newIndices.reserve(int(connectedCompVec.size()));
        newMeshIds.reserve(connectedCompVec.size());
        for (size_t i = 0; i < connectedCompVec.size(); ++i) {
            connectedCompVec[i].second->SetS();
            vcg::tri::UpdateSelection<VCGMesh>::FaceConnectedFF(entry.mesh);
            vcg::tri::UpdateSelection<VCGMesh>::VertexFromFaceLoose(entry.mesh);
            VCGMesh componentMesh;
            vcg::tri::Append<VCGMesh, VCGMesh>::Mesh(componentMesh, entry.mesh, true);
            vcg::tri::Allocator<VCGMesh>::CompactEveryVector(componentMesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(componentMesh);
            if (componentMesh.FN() > 0)
                vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(componentMesh);
            const int newIndex = addDerivedMesh(
                doc,
                componentMesh,
                QObject::tr("CC %1").arg(i),
                entry.ioMask,
                entry,
                entry.transform);
            if (newIndex < 0)
                return fail(QObject::tr("Failed to create connected component layer %1.").arg(i));
            newIndices.push_back(newIndex);
            newMeshIds.push_back(doc.mesh(newIndex).meshId);
            Sel::FaceClear(entry.mesh);
            Sel::VertexClear(entry.mesh);
        }

        const bool deleteSourceMesh = params.getBool(QStringLiteral("delete_source_mesh"), false);
        if (deleteSourceMesh)
            doc.removeMesh(currentIndex);

        if (deleteSourceMesh) {
            newIndices.clear();
            for (std::uint64_t meshId : newMeshIds) {
                const int idx = findMeshIndexById(doc, meshId);
                if (idx >= 0)
                    newIndices.push_back(idx);
            }
        }

        return successInfo(true,
            { QObject::tr("Split current mesh into %1 connected component layer(s).").arg(numCC) },
            newIndices);
    }

    if (filterId == QString::fromLatin1(kFlatten)) {
        const bool mergeVisible = params.getBool(QStringLiteral("MergeVisible"), true);
        const bool deleteLayer = params.getBool(QStringLiteral("DeleteLayer"), true);
        const bool mergeVertices = params.getBool(QStringLiteral("MergeVertices"), true);
        const bool alsoUnreferenced = params.getBool(QStringLiteral("AlsoUnreferenced"), true);

        std::vector<int> sourceIndices;
        for (int i = 0; i < doc.meshCount(); ++i) {
            if (doc.mesh(i).visible || !mergeVisible)
                sourceIndices.push_back(i);
        }
        if (sourceIndices.empty())
            return fail(QObject::tr("No source layers available for flattening."));

        VCGMesh mergedMesh;
        for (int index : sourceIndices) {
            auto part = meshCopy(doc.mesh(index).mesh);
            if (!alsoUnreferenced)
                vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(*part);
            applyTransformToMesh(*part, doc.mesh(index).transform);
            vcg::tri::Append<VCGMesh, VCGMesh>::Mesh(mergedMesh, *part, false);
        }

        if (mergeVertices)
            vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(mergedMesh);
        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mergedMesh);
        vcg::tri::UpdateBounding<VCGMesh>::Box(mergedMesh);
        if (mergedMesh.FN() > 0)
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mergedMesh);

        const int mergedMask = unionIoMask(doc, sourceIndices);
        const int newIndex = doc.addMesh(mergedMesh, mergedLayerName(), mergedMask);
        if (newIndex < 0)
            return fail(QObject::tr("Failed to create merged layer."));
        const std::uint64_t mergedMeshId = doc.mesh(newIndex).meshId;
        const QString mergedName = doc.mesh(newIndex).name;
        doc.mesh(newIndex).transform.setToIdentity();

        if (deleteLayer) {
            std::sort(sourceIndices.rbegin(), sourceIndices.rend());
            for (int index : sourceIndices)
                if (index >= 0 && index < doc.meshCount() && index != newIndex)
                    doc.removeMesh(index);
        }

        const int finalMergedIndex = findMeshIndexById(doc, mergedMeshId);
        QVector<int> resultNewIndices;
        if (finalMergedIndex >= 0)
            resultNewIndices.push_back(finalMergedIndex);

        return successInfo(true,
            {
                QObject::tr("Merged %1 layer(s) into '%2'.").arg(sourceIndices.size()).arg(mergedName),
                mergeVertices ? QObject::tr("Duplicate vertices were merged.") : QObject::tr("Duplicate vertices were kept.")
            },
            resultNewIndices);
    }

    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerLayerFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<LayerFilterPlugin>());
}
