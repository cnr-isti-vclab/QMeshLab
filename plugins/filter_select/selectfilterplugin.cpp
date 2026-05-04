#include "selectfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <QColor>
#include <QVector3D>
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/point_outlier.h>
#include <vcg/complex/algorithms/stat.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/math/base.h>
#include <vcg/space/colorspace.h>
#include <vcg/space/triangle3.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr QLatin1StringView kFilterSelectAll("select_all");
constexpr QLatin1StringView kFilterSelectNone("select_none");
constexpr QLatin1StringView kFilterSelectByAngle("select_by_view_angle");
constexpr QLatin1StringView kFilterSelectUgly("select_problematic_faces");
constexpr QLatin1StringView kFilterSelectInvert("select_invert");
constexpr QLatin1StringView kFilterSelectConnected("select_connected_faces");
constexpr QLatin1StringView kFilterSelectFaceFromVert("select_faces_from_vertices");
constexpr QLatin1StringView kFilterSelectVertFromFace("select_vertices_from_faces");
constexpr QLatin1StringView kFilterDeleteSelectedVerts("delete_selected_vertices");
constexpr QLatin1StringView kFilterDeleteAllFaces("delete_all_faces");
constexpr QLatin1StringView kFilterDeleteSelectedFaces("delete_selected_faces");
constexpr QLatin1StringView kFilterDeleteSelectedFaceVerts("delete_selected_faces_and_vertices");
constexpr QLatin1StringView kFilterSelectErode("select_erode");
constexpr QLatin1StringView kFilterSelectDilate("select_dilate");
constexpr QLatin1StringView kFilterSelectBorder("select_border");
constexpr QLatin1StringView kFilterSelectByFaceQuality("select_by_face_quality");
constexpr QLatin1StringView kFilterSelectByVertQuality("select_by_vertex_quality");
constexpr QLatin1StringView kFilterSelectByColor("select_by_color");
constexpr QLatin1StringView kFilterSelectSelfIntersect("select_self_intersecting_faces");
constexpr QLatin1StringView kFilterSelectTexBorder("select_vertex_texture_seams");
constexpr QLatin1StringView kFilterSelectNonManifoldFace("select_non_manifold_edges");
constexpr QLatin1StringView kFilterSelectNonManifoldVertex("select_non_manifold_vertices");
constexpr QLatin1StringView kFilterSelectFacesByEdge("select_faces_by_edge_length");
constexpr QLatin1StringView kFilterSelectOutlier("select_outliers");


void updateGeometryAfterDeletion(VCGMesh &mesh)
{
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    if (mesh.FN() > 0)
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

}

QString SelectFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.select");
}

QString SelectFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Selection Filters");
}

MeshFilterRunResult SelectFilterPlugin::runFilter(
    const QString &filterId,
    const FilterParams &params,
    Document &doc) const
{
    using Mask = vcg::tri::io::Mask;
    using Sel = vcg::tri::UpdateSelection<VCGMesh>;

    auto fail = [](const QString &msg) {
        MeshFilterRunResult result;
        result.success = false;
        result.documentModified = false;
        result.errorMessage = msg;
        return result;
    };

    auto selectionSummary = [](const VCGMesh &mesh) {
        return QObject::tr("Selection now contains %1 / %2 vertices and %3 / %4 faces.")
            .arg(Sel::VertexCount(mesh))
            .arg(mesh.VN())
            .arg(Sel::FaceCount(mesh))
            .arg(mesh.FN());
    };

    auto selectionResult = [&](int meshIndex, Document::MeshEntry &entry, const QString &changeMsg, QStringList extra = {}) {
        entry.ioMask |= (Mask::IOM_VERTFLAGS | Mask::IOM_FACEFLAGS);
        doc.markMeshSelectionChanged(meshIndex, changeMsg);
        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = std::move(extra);
        result.infoMessages.push_back(selectionSummary(entry.mesh));
        return result;
    };

    auto interruptResult = []() {
        return MeshFilterRunResult{ false, false, QObject::tr("Filter interrupted by user.") };
    };

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex < 0 || meshIndex >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    Document::MeshEntry &entry = doc.mesh(meshIndex);
    VCGMesh &mesh = entry.mesh;
    vcg::CallBackPos *cb = doc.progressCallback();

    if (filterId == QString::fromLatin1(kFilterSelectAll)) {
        if (params.getBool(QStringLiteral("allVerts")))
            Sel::VertexAll(mesh);
        if (params.getBool(QStringLiteral("allFaces")))
            Sel::FaceAll(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Select all on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectNone)) {
        if (params.getBool(QStringLiteral("allVerts")))
            Sel::VertexClear(mesh);
        if (params.getBool(QStringLiteral("allFaces")))
            Sel::FaceClear(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Clear selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectByAngle)) {
        if (mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh has no faces."));

        if (params.getBool(QStringLiteral("usecamera"))) {
            return fail(QObject::tr(
                "Use ViewPoint from Mesh Camera is not supported in current QMeshLab data model."));
        }

        const QVector3D vp = params.getPoint3f(QStringLiteral("viewpoint"));
        const vcg::Point3f viewpoint(float(vp.x()), float(vp.y()), float(vp.z()));
        const float angleDeg = float(params.getDouble(QStringLiteral("anglelimit")));
        const float limit = std::cos(vcg::math::ToRad(angleDeg));

        int selected = 0;
        for (VCGFace &f : mesh.face) {
            vcg::Point3f viewray = vcg::Barycenter(f) - viewpoint;
            const float nrm = std::sqrt(viewray.SquaredNorm());
            if (nrm <= 1e-20f)
                continue;
            viewray /= nrm;
            vcg::Point3f n = f.cN();
            const float nn = std::sqrt(n.SquaredNorm());
            if (nn <= 1e-20f)
                continue;
            n /= nn;
            if (viewray.dot(n) < limit) {
                if (!f.IsS())
                    ++selected;
                f.SetS();
            }
        }

        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Select faces by view angle on '%1'").arg(entry.name),
            { QObject::tr("Marked %1 faces by angle threshold %2°.")
                    .arg(selected)
                    .arg(QString::number(angleDeg, 'f', 2)) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectUgly)) {
        if (mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh has no faces."));

        Sel::Clear(mesh);
        int selectedByAR = 0;
        int selectedByNF = 0;
        int selectedByFolded = 0;

        if (params.getBool(QStringLiteral("useAR"))) {
            const float aRatio = float(params.getDouble(QStringLiteral("ARatio")));
            for (VCGFace &f : mesh.face) {
                const float q = vcg::QualityRadii(f.V(0)->P(), f.V(1)->P(), f.V(2)->P());
                if (q < aRatio) {
                    if (!f.IsS())
                        ++selectedByAR;
                    f.SetS();
                }
            }
        }

        if (params.getBool(QStringLiteral("useNF"))) {
            VCGMeshFFAdjScope _ffAdj(mesh);
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(mesh);
            const float nfRatio = float(params.getDouble(QStringLiteral("NFRatio")));
            for (VCGFace &f : mesh.face) {
                float worstAngle = 0.0f;
                for (int ei = 0; ei < 3; ++ei) {
                    VCGFace *adjf = f.FFp(ei);
                    if (!adjf || adjf == &f)
                        continue;
                    vcg::Point3f n0 = f.N();
                    vcg::Point3f n1 = adjf->N();
                    const float nn0 = std::sqrt(n0.SquaredNorm());
                    const float nn1 = std::sqrt(n1.SquaredNorm());
                    if (nn0 <= 1e-20f || nn1 <= 1e-20f)
                        continue;
                    n0 /= nn0;
                    n1 /= nn1;
                    const float dot = std::clamp(n0.dot(n1), -1.0f, 1.0f);
                    const float angle = vcg::math::ToDeg(std::fabs(std::acos(dot)));
                    worstAngle = std::max(worstAngle, angle);
                }
                if (worstAngle > nfRatio) {
                    if (!f.IsS())
                        ++selectedByNF;
                    f.SetS();
                }
            }
        }

        if (params.getBool(QStringLiteral("select_folded_faces"))) {
            const float angleThr =
                float(params.getDouble(QStringLiteral("folded_faces_angle_threshold")));
            const int beforeSel = int(Sel::FaceCount(mesh));
            VCGMeshVFAdjScope _vfAdj(mesh);
            vcg::tri::UpdateTopology<VCGMesh>::VertexFace(mesh);
            vcg::tri::Clean<VCGMesh>::SelectFoldedFaceFromOneRingFaces(
                mesh,
                std::cos(vcg::math::ToRad(angleThr)));
            selectedByFolded = std::max(0, int(Sel::FaceCount(mesh)) - beforeSel);
        }

        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Select problematic faces on '%1'").arg(entry.name),
            {
                QObject::tr("Selected by aspect ratio: %1").arg(selectedByAR),
                QObject::tr("Selected by normal angle: %1").arg(selectedByNF),
                QObject::tr("Selected folded faces: %1").arg(selectedByFolded)
            });
    }

    if (filterId == QString::fromLatin1(kFilterSelectInvert)) {
        if (params.getBool(QStringLiteral("InvVerts")))
            Sel::VertexInvert(mesh);
        if (params.getBool(QStringLiteral("InvFaces")))
            Sel::FaceInvert(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Invert selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectConnected)) {
        if (mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh has no faces."));
        Sel::FaceConnectedFF(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Expanded connected face selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectFaceFromVert)) {
        const bool strict = params.getBool(QStringLiteral("Inclusive"));
        if (strict)
            Sel::FaceFromVertexStrict(mesh);
        else
            Sel::FaceFromVertexLoose(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Transferred vertex selection to faces on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectVertFromFace)) {
        const bool strict = params.getBool(QStringLiteral("Inclusive"));
        if (strict)
            Sel::VertexFromFaceStrict(mesh);
        else
            Sel::VertexFromFaceLoose(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Transferred face selection to vertices on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterDeleteSelectedVerts)) {
        const int selectedVerts = Sel::VertexCount(mesh);
        if (selectedVerts == 0) {
            MeshFilterRunResult result;
            result.success = true;
            result.documentModified = false;
            result.infoMessages = { QObject::tr("Nothing done: no vertex selected.") };
            return result;
        }

        const int beforeV = mesh.VN();
        const int beforeF = mesh.FN();
        Sel::FaceClear(mesh);
        Sel::FaceFromVertexLoose(mesh);
        for (VCGFace &f : mesh.face) {
            if (f.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, f);
        }
        for (VCGVertex &v : mesh.vert) {
            if (v.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteVertex(mesh, v);
        }
        updateGeometryAfterDeletion(mesh);
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Deleted selected vertices from '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Deleted %1 vertices, %2 faces.")
                .arg(beforeV - mesh.VN())
                .arg(beforeF - mesh.FN())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDeleteAllFaces)) {
        const bool allLayers = params.getBool(QStringLiteral("allLayers"));
        int changedLayers = 0;
        int totalDeletedFaces = 0;
        QStringList info;

        if (allLayers) {
            for (int i = 0; i < doc.meshCount(); ++i) {
                Document::MeshEntry &layer = doc.mesh(i);
                if (!layer.visible)
                    continue;
                VCGMesh &layerMesh = layer.mesh;
                const int before = layerMesh.FN();
                if (before <= 0)
                    continue;
                for (VCGFace &f : layerMesh.face) {
                        vcg::tri::Allocator<VCGMesh>::DeleteFace(layerMesh, f);
                }
                updateGeometryAfterDeletion(layerMesh);
                doc.markMeshGeometryChanged(
                    i,
                    QObject::tr("Deleted all faces from '%1'.").arg(layer.name));
                const int deleted = before - layerMesh.FN();
                totalDeletedFaces += deleted;
                ++changedLayers;
                info.push_back(
                    QObject::tr("Layer '%1': deleted %2 faces.")
                        .arg(layer.name)
                        .arg(deleted));
            }
        } else {
            const int before = mesh.FN();
            if (before > 0) {
                for (VCGFace &f : mesh.face) {
                        vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, f);
                }
                updateGeometryAfterDeletion(mesh);
                doc.markMeshGeometryChanged(
                    meshIndex,
                    QObject::tr("Deleted all faces from '%1'.").arg(entry.name));
                const int deleted = before - mesh.FN();
                totalDeletedFaces = deleted;
                changedLayers = 1;
                info.push_back(QObject::tr("Deleted all %1 faces.").arg(deleted));
            }
        }

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = (changedLayers > 0);
        if (changedLayers == 0)
            info.push_back(QObject::tr("Nothing done: no faces found in target layers."));
        result.infoMessages = info;
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDeleteSelectedFaces)) {
        const int selectedFaces = Sel::FaceCount(mesh);
        if (selectedFaces == 0) {
            MeshFilterRunResult result;
            result.success = true;
            result.documentModified = false;
            result.infoMessages = { QObject::tr("Nothing done: no faces selected.") };
            return result;
        }

        const int beforeF = mesh.FN();
        for (VCGFace &f : mesh.face) {
            if (f.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, f);
        }
        updateGeometryAfterDeletion(mesh);
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Deleted selected faces from '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Deleted %1 faces.")
                .arg(beforeF - mesh.FN())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterDeleteSelectedFaceVerts)) {
        const int selectedFaces = Sel::FaceCount(mesh);
        if (selectedFaces == 0) {
            MeshFilterRunResult result;
            result.success = true;
            result.documentModified = false;
            result.infoMessages = { QObject::tr("Nothing done: no faces selected.") };
            return result;
        }

        const int beforeV = mesh.VN();
        const int beforeF = mesh.FN();
        Sel::VertexClear(mesh);
        Sel::VertexFromFaceStrict(mesh);
        for (VCGFace &f : mesh.face) {
            if (f.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, f);
        }
        for (VCGVertex &v : mesh.vert) {
            if (v.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteVertex(mesh, v);
        }
        updateGeometryAfterDeletion(mesh);
        doc.markMeshGeometryChanged(
            meshIndex,
            QObject::tr("Deleted selected faces and vertices from '%1'.").arg(entry.name));

        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = true;
        result.infoMessages = {
            QObject::tr("Deleted %1 faces, %2 vertices.")
                .arg(beforeF - mesh.FN())
                .arg(beforeV - mesh.VN())
        };
        return result;
    }

    if (filterId == QString::fromLatin1(kFilterSelectErode)) {
        Sel::VertexFromFaceStrict(mesh);
        Sel::FaceFromVertexStrict(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Erode selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectDilate)) {
        Sel::VertexFromFaceLoose(mesh);
        Sel::FaceFromVertexLoose(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Dilate selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectBorder)) {
        vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromNone(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::VertexBorderFromFaceBorder(mesh);
        Sel::FaceFromBorderFlag(mesh);
        Sel::VertexFromBorderFlag(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected border on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectByVertQuality)) {
        const float minQ = float(params.getDouble(QStringLiteral("minQ")));
        const float maxQ = float(params.getDouble(QStringLiteral("maxQ")));
        const bool inclusive = params.getBool(QStringLiteral("Inclusive"));
        Sel::VertexFromQualityRange(mesh, minQ, maxQ);
        if (inclusive)
            Sel::FaceFromVertexStrict(mesh);
        else
            Sel::FaceFromVertexLoose(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected by vertex quality on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectByFaceQuality)) {
        const float minQ = float(params.getDouble(QStringLiteral("minQ")));
        const float maxQ = float(params.getDouble(QStringLiteral("maxQ")));
        const bool inclusive = params.getBool(QStringLiteral("Inclusive"));
        Sel::FaceFromQualityRange(mesh, minQ, maxQ);
        if (inclusive)
            Sel::VertexFromFaceStrict(mesh);
        else
            Sel::VertexFromFaceLoose(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected by face quality on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectByColor)) {
        const QColor targetColor = params.getColor(QStringLiteral("Color"));
        const QString colorSpace =
            params.getEnum(QStringLiteral("ColorSpace")).toLower();
        const bool inclusive = params.getBool(QStringLiteral("Inclusive"));
        const float valueRH = float(params.getDouble(QStringLiteral("PercentRH")));
        const float valueGS = float(params.getDouble(QStringLiteral("PercentGS")));
        const float valueBV = float(params.getDouble(QStringLiteral("PercentBV")));

        const float red = targetColor.redF();
        const float green = targetColor.greenF();
        const float blue = targetColor.blueF();
        float hue = targetColor.hueF();
        if (hue < 0.0f)
            hue = 0.0f;
        const float saturation = targetColor.saturationF();
        const float value = targetColor.valueF();

        Sel::FaceClear(mesh);
        Sel::VertexClear(mesh);

        for (VCGVertex &v : mesh.vert) {
            vcg::Color4f cv = vcg::Color4f::Construct(v.C());
            if (colorSpace == QStringLiteral("hsv")) {
                cv = vcg::ColorSpace<float>::RGBtoHSV(cv);
                if (std::fabs(cv[0] - hue) <= valueRH
                    && std::fabs(cv[1] - saturation) <= valueGS
                    && std::fabs(cv[2] - value) <= valueBV) {
                    v.SetS();
                }
            } else {
                if (std::fabs(cv[0] - red) <= valueRH
                    && std::fabs(cv[1] - green) <= valueGS
                    && std::fabs(cv[2] - blue) <= valueBV) {
                    v.SetS();
                }
            }
        }

        if (inclusive)
            Sel::FaceFromVertexStrict(mesh);
        else
            Sel::FaceFromVertexLoose(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected by color on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectTexBorder)) {
        VCGMeshFFAdjScope _ffAdj(mesh);
        vcg::tri::UpdateTopology<VCGMesh>::FaceFaceFromTexCoord(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::VertexBorderFromFaceBorder(mesh);
        Sel::VertexFromBorderFlag(mesh);
        // Restore standard topology and border flags.
        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
        vcg::tri::UpdateFlags<VCGMesh>::VertexBorderFromFaceBorder(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected texture seams on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectNonManifoldFace)) {

        const int nm = vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh, true);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected non manifold edges on '%1'").arg(entry.name),
            { QObject::tr("Non manifold edges found: %1").arg(nm) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectNonManifoldVertex)) {

        const int nm = vcg::tri::Clean<VCGMesh>::CountNonManifoldVertexFF(mesh, true);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected non manifold vertices on '%1'").arg(entry.name),
            { QObject::tr("Non manifold vertices found: %1").arg(nm) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectSelfIntersect)) {
        std::vector<VCGFace *> intersFaces;
        vcg::tri::Clean<VCGMesh>::SelfIntersections(mesh, intersFaces);
        Sel::FaceClear(mesh);
        for (VCGFace *f : intersFaces) {
            if (f)
                f->SetS();
        }
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected self intersecting faces on '%1'").arg(entry.name),
            { QObject::tr("Self intersecting faces: %1").arg(int(intersFaces.size())) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectFacesByEdge)) {
        const float threshold = float(params.getDouble(QStringLiteral("Threshold")));
        const int selFaceNum = Sel::FaceOutOfRangeEdge(mesh, 0.0f, threshold);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected faces by edge length on '%1'").arg(entry.name),
            { QObject::tr("Selected %1 faces with an edge longer than %2.")
                    .arg(selFaceNum)
                    .arg(QString::number(threshold, 'f', 6)) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectOutlier)) {
        if (mesh.VN() <= 0)
            return fail(QObject::tr("Current mesh has no vertices."));
        const float threshold = float(params.getDouble(QStringLiteral("PropThreshold")));
        const int kNearest = std::max(1, params.getInt(QStringLiteral("KNearest")));
        vcg::VertexConstDataWrapper<VCGMesh> wrapper(mesh);
        vcg::KdTree<VCGMesh::ScalarType> kdTree(wrapper);
        const int selVertexNum =
            vcg::tri::OutlierRemoval<VCGMesh>::SelectLoOPOutliers(mesh, kdTree, kNearest, threshold);
        if (doc.isOperationCancelRequested())
            return interruptResult();
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected outliers on '%1'").arg(entry.name),
            { QObject::tr("Selected %1 outlier vertices.").arg(selVertexNum) });
    }

    return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
}

void registerSelectFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<SelectFilterPlugin>());
}
