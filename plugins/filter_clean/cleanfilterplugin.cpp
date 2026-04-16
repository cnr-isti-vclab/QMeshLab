#include "cleanfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <wrap/io_trimesh/io_mask.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/closest.h>
#include <vcg/complex/algorithms/create/ball_pivoting.h>
#include <vcg/complex/algorithms/stat.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/complex/algorithms/update/texture.h>
#include <vcg/space/distance3.h>
#include <vcg/space/index/grid_static_ptr.h>
#include <vcg/space/triangle3.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {
constexpr QLatin1StringView kFilterBallPivoting("surface_reconstruction_ball_pivoting");
constexpr QLatin1StringView kFilterRemoveWrtQ("remove_vertices_wrt_quality");
constexpr QLatin1StringView kFilterRemoveIsolatedComplexity("remove_isolated_pieces_face_num");
constexpr QLatin1StringView kFilterRemoveIsolatedDiameter("remove_isolated_pieces_diameter");
constexpr QLatin1StringView kFilterRemoveTVertex("remove_t_vertices");
constexpr QLatin1StringView kFilterSnapMismatchedBorder("snap_mismatched_borders");
constexpr QLatin1StringView kFilterMergeCloseVertex("merge_close_vertices");
constexpr QLatin1StringView kFilterMergeWedgeTex("merge_wedge_texture_coord");
constexpr QLatin1StringView kFilterRemoveDuplicateFace("remove_duplicate_faces");
constexpr QLatin1StringView kFilterRemoveFoldFace("remove_folded_faces_by_edge_flip");
constexpr QLatin1StringView kFilterRepairNonManifEdge("repair_non_manifold_edges");
constexpr QLatin1StringView kFilterRemoveNonManifVert("repair_non_manifold_vertices_split");
constexpr QLatin1StringView kFilterRemoveUnrefVertex("remove_unreferenced_vertices");
constexpr QLatin1StringView kFilterRemoveDuplicatedVertex("remove_duplicated_vertices");
constexpr QLatin1StringView kFilterRemoveFaceZeroArea("remove_zero_area_faces");

int intParameter(const MeshFilterParameterValues &params, const QString &id, int fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const int value = it.value().toInt(&ok);
    return ok ? value : fallback;
}

double doubleParameter(const MeshFilterParameterValues &params, const QString &id, double fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const double value = it.value().toDouble(&ok);
    return ok ? value : fallback;
}

bool boolParameter(const MeshFilterParameterValues &params, const QString &id, bool fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::Bool)
        return it.value().toBool();
    const QString text = it.value().toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1"))
        return true;
    if (text == QStringLiteral("false") || text == QStringLiteral("0"))
        return false;
    return fallback;
}

QString enumParameter(
    const MeshFilterParameterValues &params,
    const QString &id,
    const QString &fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    const QString value = it.value().toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

struct CurrentMeshRef {
    int index = -1;
    Document::MeshEntry *entry = nullptr;
};

std::optional<CurrentMeshRef> currentMesh(Document &doc, QString &errorMessage)
{
    const int index = doc.currentMeshIndex();
    if (index < 0 || index >= doc.meshCount()) {
        errorMessage = QObject::tr("No current mesh selected.");
        return std::nullopt;
    }
    return CurrentMeshRef{ index, &doc.mesh(index) };
}

void compactAndUpdateGeometry(VCGMesh &mesh)
{
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    if (mesh.FN() > 0)
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

struct SnapBorderResult {
    int splitFaces = 0;
    bool interrupted = false;
};

SnapBorderResult snapMismatchedBorder(
    VCGMesh &mesh,
    const float threshold,
    vcg::CallBackPos *cb)
{
    using Scalar = VCGMesh::ScalarType;
    using Point = vcg::Point3<Scalar>;
    using FacePointer = VCGMesh::FacePointer;
    using MetroMeshFaceGrid = vcg::GridStaticPtr<VCGMesh::FaceType, Scalar>;

    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
    vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
    vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
    vcg::tri::UpdateFlags<VCGMesh>::VertexBorderFromFaceBorder(mesh);
    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);

    MetroMeshFaceGrid unifGridFace;
    vcg::tri::FaceTmark<VCGMesh> markerFunctor(&mesh);
    vcg::face::PointDistanceBaseFunctor<Scalar> pointDistanceFunctor;
    vcg::tri::UpdateFlags<VCGMesh>::FaceClearV(mesh);
    unifGridFace.Set(mesh.face.begin(), mesh.face.end());

    const int kClosestCount = 20;
    const float maxDist = mesh.bbox.Diag() / 20.0f;
    std::vector<Point> splitVertices;
    std::vector<FacePointer> splitFaces;
    std::vector<int> splitEdges;

    const int vn = std::max(1, mesh.VN());
    for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
        if (vi->IsD() || !vi->IsB())
            continue;

        if (cb) {
            const int progress = std::clamp(
                int((int(vcg::tri::Index(mesh, *vi)) * 100) / vn),
                0,
                100);
            if (!(*cb)(progress, "Snapping vertices"))
                return { int(splitVertices.size()), true };
        }

        std::vector<FacePointer> faceVec;
        std::vector<float> distVec;
        std::vector<Point> pointVec;
        const Point startPt = vi->P();
        const int faceFound = unifGridFace.GetKClosest(
            pointDistanceFunctor,
            markerFunctor,
            kClosestCount,
            startPt,
            maxDist,
            faceVec,
            distVec,
            pointVec);

        FacePointer bestFace = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        int bestEdge = -1;
        Point bestPoint = vi->cP();
        for (int i = 0; i < faceFound; ++i) {
            const float epsilonSmall = 1e-5f;
            const float epsilonBig = 1e-2f;
            FacePointer fp = faceVec[static_cast<size_t>(i)];
            Point bary;
            vcg::InterpolationParameters(*fp, fp->cN(), pointVec[static_cast<size_t>(i)], bary);

            for (int j = 0; j < 3; ++j) {
                if (!vcg::face::IsBorder(*fp, j) || fp->IsV())
                    continue;
                if (bary[(j + 0) % 3] <= epsilonBig || bary[(j + 1) % 3] <= epsilonBig)
                    continue;
                if (bary[(j + 2) % 3] >= epsilonSmall)
                    continue;

                const float d = distVec[static_cast<size_t>(i)];
                if (d >= bestDist)
                    continue;
                bestDist = d;
                bestFace = fp;
                bestEdge = j;
                bestPoint = vi->cP();
            }
        }

        if (!bestFace || bestEdge < 0)
            continue;

        const float localThr =
            threshold * vcg::Distance(bestFace->P0(bestEdge), bestFace->P1(bestEdge));
        if (bestDist >= localThr || bestFace->IsV())
            continue;

        bestFace->SetV();
        vi->SetS();
        splitVertices.push_back(bestPoint);
        splitEdges.push_back(bestEdge);
        splitFaces.push_back(bestFace);
    }

    vcg::tri::Allocator<VCGMesh>::PointerUpdater<VCGMesh::FacePointer> pu;
    auto firstVert = vcg::tri::Allocator<VCGMesh>::AddVertices(mesh, splitVertices.size());
    auto firstFace = vcg::tri::Allocator<VCGMesh>::AddFaces(mesh, splitVertices.size(), pu);

    for (size_t i = 0; i < splitVertices.size(); ++i) {
        firstVert->P() = splitVertices[i];
        const int edgeIndex = splitEdges[i];
        FacePointer fp = splitFaces[i];
        pu.Update(fp);

        firstFace->V(0) = &(*firstVert);
        firstFace->V(1) = fp->V2(edgeIndex);
        firstFace->V(2) = fp->V0(edgeIndex);
        fp->V0(edgeIndex) = &(*firstVert);
        ++firstFace;
        ++firstVert;
    }

    vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
    return { int(splitVertices.size()), false };
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &doc)
{
    float bboxDiag = 1.0f;
    float qualityMin = 0.0f;
    float qualityMax = 1.0f;

    const int meshIndex = doc.currentMeshIndex();
    if (meshIndex >= 0 && meshIndex < doc.meshCount()) {
        const VCGMesh &mesh = doc.mesh(meshIndex).mesh;
        bboxDiag = std::max(1e-9f, mesh.bbox.Diag());
        if (mesh.VN() > 0) {
            const auto minMax = vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityMinMax(mesh);
            qualityMin = std::min(minMax.first, minMax.second);
            qualityMax = std::max(minMax.first, minMax.second);
        }
    }

    std::vector<MeshFilterDescriptor> out;

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterBallPivoting);
        d.menuPath = QObject::tr("Remeshing");
        d.name = QObject::tr("Surface Reconstruction: Ball Pivoting");
        d.shortDescription = QObject::tr("Reconstruct a surface from oriented points using Ball Pivoting.");
        d.longDescriptionMarkdown = QObject::tr(
            "Given a point cloud with normals it reconstructs a surface using the **Ball Pivoting "
            "Algorithm**.\n"
            "Starting with a seed triangle, the BPA algorithm pivots a ball of the given radius "
            "around the already formed edges until it touches another point, forming another "
            "triangle. The process continues until all reachable edges have been tried. This "
            "surface reconstruction algorithm uses the existing points without creating new ones. "
            "Works better with uniformly sampled point clouds. If needed first perform a poisson "
            "disk subsampling of the point cloud.\n\n"
            "Bernardini F., Mittleman J., Rushmeier H., Silva C., Taubin G.\n"
            "**The ball-pivoting algorithm for surface reconstruction.**\n"
            "IEEE TVCG 1999");
        d.tags = { QStringLiteral("remeshing"), QStringLiteral("reconstruction"), QStringLiteral("point cloud") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pBall;
        pBall.id = QStringLiteral("ball_radius");
        pBall.label = QObject::tr("Pivoting Ball Radius");
        pBall.helpMarkdown = QObject::tr(
            "The radius of the ball pivoting (rolling) over the set of points. "
            "Gaps that are larger than the ball radius will not be filled; similarly "
            "small pits smaller than the ball radius will be filled. Use `0` for autoguess.");
        pBall.group = QStringLiteral("main");
        pBall.type = MeshFilterParameterType::Double;
        pBall.defaultValue = 0.0;
        pBall.minValue = 0.0;
        pBall.maxValue = double(bboxDiag);
        pBall.decimals = 6;
        d.parameters.push_back(std::move(pBall));

        MeshFilterParameterDescriptor pCluster;
        pCluster.id = QStringLiteral("clustering_percent");
        pCluster.label = QObject::tr("Clustering Radius (%)");
        pCluster.helpMarkdown = QObject::tr(
            "To avoid creation of too small triangles, if a vertex is found too close to a "
            "previous one, it is clustered/merged with it.");
        pCluster.group = QStringLiteral("main");
        pCluster.type = MeshFilterParameterType::Double;
        pCluster.defaultValue = 20.0;
        pCluster.minValue = 0.0;
        pCluster.maxValue = 100.0;
        pCluster.decimals = 3;
        d.parameters.push_back(std::move(pCluster));

        MeshFilterParameterDescriptor pCrease;
        pCrease.id = QStringLiteral("crease_threshold_deg");
        pCrease.label = QObject::tr("Angle Threshold (degrees)");
        pCrease.helpMarkdown = QObject::tr(
            "If we encounter a crease angle that is too large we should stop the ball rolling.");
        pCrease.group = QStringLiteral("main");
        pCrease.type = MeshFilterParameterType::Double;
        pCrease.defaultValue = 90.0;
        pCrease.minValue = 0.0;
        pCrease.maxValue = 180.0;
        pCrease.decimals = 3;
        d.parameters.push_back(std::move(pCrease));

        MeshFilterParameterDescriptor pDeleteFaces;
        pDeleteFaces.id = QStringLiteral("delete_initial_faces");
        pDeleteFaces.label = QObject::tr("Delete Initial Set of Faces");
        pDeleteFaces.helpMarkdown = QObject::tr(
            "If true all the initial faces of the mesh are deleted and the whole surface is "
            "rebuilt from scratch. Otherwise current faces are used as a starting point.");
        pDeleteFaces.group = QStringLiteral("main");
        pDeleteFaces.type = MeshFilterParameterType::Bool;
        pDeleteFaces.defaultValue = false;
        d.parameters.push_back(std::move(pDeleteFaces));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveWrtQ);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Vertices wrt Quality");
        d.shortDescription =
            QObject::tr("Delete all vertices with quality lower than a threshold.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete all the vertices with a quality lower smaller than the specified constant.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("quality"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.inputRequirements.requireVertexQuality = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pThr;
        pThr.id = QStringLiteral("max_quality_thr");
        pThr.label = QObject::tr("Delete all vertices with quality under");
        pThr.helpMarkdown = QObject::tr("Vertices with quality lower than this threshold are deleted.");
        pThr.group = QStringLiteral("main");
        pThr.type = MeshFilterParameterType::Double;
        pThr.defaultValue = double(qualityMax);
        pThr.minValue = double(qualityMin);
        pThr.maxValue = double(qualityMax);
        pThr.decimals = 6;
        d.parameters.push_back(std::move(pThr));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveIsolatedComplexity);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Isolated Pieces (wrt Face Num.)");
        d.shortDescription =
            QObject::tr("Delete isolated connected components composed by few triangles.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete isolated connected components composed by a limited number of triangles.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("components"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pSize;
        pSize.id = QStringLiteral("min_component_size");
        pSize.label = QObject::tr("Enter minimum conn. comp size");
        pSize.helpMarkdown = QObject::tr(
            "Delete all the connected components (floating pieces) composed by a number of "
            "triangles smaller than the specified one.");
        pSize.group = QStringLiteral("main");
        pSize.type = MeshFilterParameterType::Int;
        pSize.defaultValue = 25;
        pSize.minValue = 0;
        pSize.maxValue = 1000000000;
        d.parameters.push_back(std::move(pSize));

        MeshFilterParameterDescriptor pRemoveUnref;
        pRemoveUnref.id = QStringLiteral("remove_unref");
        pRemoveUnref.label = QObject::tr("Remove unreferenced vertices");
        pRemoveUnref.helpMarkdown = QObject::tr(
            "If true, the unreferenced vertices remaining after face deletion are removed.");
        pRemoveUnref.group = QStringLiteral("main");
        pRemoveUnref.type = MeshFilterParameterType::Bool;
        pRemoveUnref.defaultValue = true;
        d.parameters.push_back(std::move(pRemoveUnref));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveIsolatedDiameter);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Isolated Pieces (wrt Diameter)");
        d.shortDescription =
            QObject::tr("Delete isolated connected components whose diameter is below a threshold.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete isolated connected components whose diameter is smaller than the specified "
            "constant.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("components"), QStringLiteral("diameter") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pDiag;
        pDiag.id = QStringLiteral("min_component_diag");
        pDiag.label = QObject::tr("Enter max diameter of isolated pieces");
        pDiag.helpMarkdown = QObject::tr(
            "Delete all connected components (floating pieces) with a diameter smaller than "
            "the specified one.");
        pDiag.group = QStringLiteral("main");
        pDiag.type = MeshFilterParameterType::Double;
        pDiag.defaultValue = double(bboxDiag / 10.0f);
        pDiag.minValue = 0.0;
        pDiag.maxValue = double(bboxDiag);
        pDiag.decimals = 6;
        d.parameters.push_back(std::move(pDiag));

        MeshFilterParameterDescriptor pRemoveUnref;
        pRemoveUnref.id = QStringLiteral("remove_unref");
        pRemoveUnref.label = QObject::tr("Remove unreferenced vertices");
        pRemoveUnref.helpMarkdown = QObject::tr(
            "If true, the unreferenced vertices remaining after face deletion are removed.");
        pRemoveUnref.group = QStringLiteral("main");
        pRemoveUnref.type = MeshFilterParameterType::Bool;
        pRemoveUnref.defaultValue = true;
        d.parameters.push_back(std::move(pRemoveUnref));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveTVertex);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove T-Vertices");
        d.shortDescription =
            QObject::tr("Remove T-vertices using edge collapse or edge flip.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete t-vertices from the mesh by edge collapse (collapsing the shortest of the "
            "incident edges) or edge flip (flipping the opposite edge on the degenerate face if "
            "the triangulation quality improves).");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("t-vertex"), QStringLiteral("topology") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pMethod;
        pMethod.id = QStringLiteral("method");
        pMethod.label = QObject::tr("Method");
        pMethod.helpMarkdown = QObject::tr(
            "Selects whether to remove t-vertices by edge collapse or edge flip.");
        pMethod.group = QStringLiteral("main");
        pMethod.type = MeshFilterParameterType::Enum;
        pMethod.defaultValue = QStringLiteral("edge_collapse");
        pMethod.enumOptions = {
            { QStringLiteral("edge_collapse"), QObject::tr("Edge Collapse"), {} },
            { QStringLiteral("edge_flip"), QObject::tr("Edge Flip"), {} }
        };
        d.parameters.push_back(std::move(pMethod));

        MeshFilterParameterDescriptor pRatio;
        pRatio.id = QStringLiteral("threshold");
        pRatio.label = QObject::tr("Ratio");
        pRatio.helpMarkdown = QObject::tr(
            "Detects faces where the base/height ratio is lower than this value.");
        pRatio.group = QStringLiteral("main");
        pRatio.type = MeshFilterParameterType::Double;
        pRatio.defaultValue = 40.0;
        pRatio.minValue = 0.0;
        pRatio.maxValue = 1e9;
        pRatio.decimals = 6;
        d.parameters.push_back(std::move(pRatio));

        MeshFilterParameterDescriptor pRepeat;
        pRepeat.id = QStringLiteral("repeat");
        pRepeat.label = QObject::tr("Iterate until convergence");
        pRepeat.helpMarkdown = QObject::tr("Iterates the algorithm until it reaches convergence.");
        pRepeat.group = QStringLiteral("main");
        pRepeat.type = MeshFilterParameterType::Bool;
        pRepeat.defaultValue = true;
        d.parameters.push_back(std::move(pRepeat));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSnapMismatchedBorder);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Snap Mismatched Borders");
        d.shortDescription =
            QObject::tr("Try to snap together slightly mismatched adjacent borders.");
        d.longDescriptionMarkdown = QObject::tr(
            "Try to snap together adjacent borders that are slightly mismatched.\n"
            "This situation can happen on badly triangulated adjacent patches defined by high "
            "order surfaces.\n"
            "For each border vertex the filter snaps it onto the closest boundary edge only if "
            "it is closer than `edge_length * threshold`. When a vertex is snapped the "
            "corresponding face is split and a new vertex is created.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("border"), QStringLiteral("snap") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pRatio;
        pRatio.id = QStringLiteral("edge_dist_ratio");
        pRatio.label = QObject::tr("Edge Distance Ratio");
        pRatio.helpMarkdown = QObject::tr(
            "Collapse edge when the edge / distance ratio is greater than this value. "
            "Larger values enforce that only vertices very close to the line are removed.");
        pRatio.group = QStringLiteral("main");
        pRatio.type = MeshFilterParameterType::Double;
        pRatio.defaultValue = 1.0 / 100.0;
        pRatio.minValue = 0.0;
        pRatio.maxValue = 1000.0;
        pRatio.decimals = 6;
        d.parameters.push_back(std::move(pRatio));

        MeshFilterParameterDescriptor pUnify;
        pUnify.id = QStringLiteral("unify_vertices");
        pUnify.label = QObject::tr("Unify Vertices");
        pUnify.helpMarkdown = QObject::tr("If true, snapped vertices are welded together.");
        pUnify.group = QStringLiteral("main");
        pUnify.type = MeshFilterParameterType::Bool;
        pUnify.defaultValue = true;
        d.parameters.push_back(std::move(pUnify));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterMergeCloseVertex);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Merge Close Vertices");
        d.shortDescription = QObject::tr("Merge vertices that are nearer than a threshold.");
        d.longDescriptionMarkdown = QObject::tr(
            "Merge together all the vertices that are nearer than the specified threshold. "
            "Like a unify duplicated vertices but with some tolerance.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("merge"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pThr;
        pThr.id = QStringLiteral("threshold");
        pThr.label = QObject::tr("Merging Distance");
        pThr.helpMarkdown = QObject::tr(
            "All vertices closer than this threshold are merged together. "
            "Use very small values; default is 1/10000 of bounding box diagonal.");
        pThr.group = QStringLiteral("main");
        pThr.type = MeshFilterParameterType::Double;
        pThr.defaultValue = double(bboxDiag / 10000.0f);
        pThr.minValue = 0.0;
        pThr.maxValue = double(bboxDiag / 100.0f);
        pThr.decimals = 8;
        d.parameters.push_back(std::move(pThr));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterMergeWedgeTex);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Merge Wedge Texture Coord");
        d.shortDescription = QObject::tr("Merge very close per-wedge texture coordinates.");
        d.longDescriptionMarkdown = QObject::tr(
            "Merge together per-wedge texture coords that are very close. "
            "Used to correct apparent texture seams that can arise from numerical "
            "approximations when saving in ascii formats.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("texture"), QStringLiteral("uv") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireTextureCoordinates = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pThr;
        pThr.id = QStringLiteral("merge_thr");
        pThr.label = QObject::tr("Merging Threshold");
        pThr.helpMarkdown = QObject::tr(
            "All per-wedge texture coords that are on the same vertex and are distant less "
            "than the threshold are merged together. Distance is in texture space.");
        pThr.group = QStringLiteral("main");
        pThr.type = MeshFilterParameterType::Double;
        pThr.defaultValue = 1.0 / 10000.0;
        pThr.minValue = 0.0;
        pThr.maxValue = 1.0;
        pThr.decimals = 8;
        d.parameters.push_back(std::move(pThr));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveDuplicateFace);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Duplicate Faces");
        d.shortDescription = QObject::tr("Delete all duplicate faces.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete all the duplicate faces. Two faces are considered equal if they are composed "
            "by the same set of vertices, regardless of the order of the vertices.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("duplicate"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveFoldFace);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Isolated Folded Faces by Edge Flip");
        d.shortDescription = QObject::tr("Remove isolated folded faces by edge flipping.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete all the single folded faces. A face is considered folded if its normal is "
            "opposite to all adjacent faces. It is removed by flipping it against the adjacent "
            "face across the edge where the opposite vertex falls inside the adjacent face.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("folded"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRepairNonManifEdge);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Repair non Manifold Edges");
        d.shortDescription =
            QObject::tr("Repair non-manifold edges by removing faces or splitting vertices.");
        d.longDescriptionMarkdown = QObject::tr(
            "Remove non-manifold edges by removing faces (for each non manifold edge it "
            "iteratively deletes the smallest area face until it becomes 2-manifold) or by "
            "splitting vertices (each non manifold edge chain becomes a border).");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("non-manifold"), QStringLiteral("edge") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pMethod;
        pMethod.id = QStringLiteral("method");
        pMethod.label = QObject::tr("Method");
        pMethod.helpMarkdown = QObject::tr(
            "Selects whether to repair non manifold edges by removing faces or by splitting "
            "vertices.");
        pMethod.group = QStringLiteral("main");
        pMethod.type = MeshFilterParameterType::Enum;
        pMethod.defaultValue = QStringLiteral("remove_faces");
        pMethod.enumOptions = {
            { QStringLiteral("remove_faces"), QObject::tr("Remove Faces"), {} },
            { QStringLiteral("split_vertices"), QObject::tr("Split Vertices"), {} }
        };
        d.parameters.push_back(std::move(pMethod));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveNonManifVert);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Repair non Manifold Vertices by Splitting");
        d.shortDescription = QObject::tr("Split non-manifold vertices until the mesh becomes 2-manifold.");
        d.longDescriptionMarkdown = QObject::tr(
            "Split non Manifold vertices until it becomes 2-Manifold.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("non-manifold"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;

        MeshFilterParameterDescriptor pDisp;
        pDisp.id = QStringLiteral("vert_disp_ratio");
        pDisp.label = QObject::tr("Vertex Displacement Ratio");
        pDisp.helpMarkdown = QObject::tr(
            "This parameter denotes the displacement ratio α. When a vertex is split, it is "
            "moved towards the barycenter of the FF-connected faces sharing it by "
            "(v-barycenter)*α. Reasonable values are in [0..0.1].");
        pDisp.group = QStringLiteral("main");
        pDisp.type = MeshFilterParameterType::Double;
        pDisp.defaultValue = 0.0;
        pDisp.minValue = 0.0;
        pDisp.maxValue = 1.0;
        pDisp.decimals = 6;
        d.parameters.push_back(std::move(pDisp));

        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveUnrefVertex);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Unreferenced Vertices");
        d.shortDescription = QObject::tr("Remove vertices that are not referenced by any face.");
        d.longDescriptionMarkdown = QObject::tr(
            "Check for every vertex on the mesh: if it is not referenced by a face, remove it.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("vertex"), QStringLiteral("unreferenced") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveDuplicatedVertex);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Duplicate Vertices");
        d.shortDescription = QObject::tr("Merge vertices that have exactly the same coordinates.");
        d.longDescriptionMarkdown = QObject::tr(
            "Check for every vertex on the mesh: if two vertices have the same coordinates they "
            "are merged into a single one.");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("duplicate"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterRemoveFaceZeroArea);
        d.menuPath = QObject::tr("Cleaning");
        d.name = QObject::tr("Remove Zero Area Faces");
        d.shortDescription = QObject::tr("Remove null faces with zero area.");
        d.longDescriptionMarkdown = QObject::tr("Remove null faces (the ones with area equal to zero).");
        d.tags = { QStringLiteral("cleaning"), QStringLiteral("face"), QStringLiteral("degenerate") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    return out;
}
}

QString CleanFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.clean");
}

QString CleanFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Cleaning Filters");
}

std::vector<MeshFilterDescriptor> CleanFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult CleanFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    using Mask = vcg::tri::io::Mask;

    QString errorMessage;
    auto current = currentMesh(doc, errorMessage);
    if (!current)
        return { false, false, errorMessage };

    Document::MeshEntry &entry = *current->entry;
    VCGMesh &mesh = entry.mesh;
    vcg::CallBackPos *cb = doc.progressCallback();

    auto interruptedResult = []() -> MeshFilterRunResult {
        return { false, false, QObject::tr("Filter interrupted by user.") };
    };

    auto successResult = [](bool modified, const QStringList &info) {
        MeshFilterRunResult result;
        result.success = true;
        result.documentModified = modified;
        result.infoMessages = info;
        return result;
    };

    if (filterId == QString::fromLatin1(kFilterBallPivoting)) {
        if (mesh.VN() <= 0)
            return { false, false, QObject::tr("Current mesh has no vertices.") };

        vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
        const float radius =
            float(doubleParameter(parameters, QStringLiteral("ball_radius"), 0.0));
        const float clustering =
            float(doubleParameter(parameters, QStringLiteral("clustering_percent"), 20.0) / 100.0);
        const float creaseThrDeg =
            float(doubleParameter(parameters, QStringLiteral("crease_threshold_deg"), 90.0));
        const float creaseThr = vcg::math::ToRad(creaseThrDeg);
        const bool deleteFaces = boolParameter(parameters, QStringLiteral("delete_initial_faces"), false);
        if (deleteFaces) {
            mesh.fn = 0;
            mesh.face.clear();
        }

        vcg::tri::UpdateTopology<VCGMesh>::VertexFace(mesh);
        const int beforeFaceCount = mesh.FN();
        vcg::tri::BallPivoting<VCGMesh> pivot(mesh, radius, clustering, creaseThr);
        pivot.BuildMesh(cb);
        if (doc.isOperationCancelRequested())
            return interruptedResult();

        compactAndUpdateGeometry(mesh);
        entry.ioMask |= Mask::IOM_FACENORMAL | Mask::IOM_VERTNORMAL;
        doc.markMeshGeometryChanged(
            current->index,
            QObject::tr("Ball-pivoting reconstruction on '%1'").arg(entry.name));
        return successResult(
            true,
            {
                QObject::tr("Reconstructed surface. Added %1 faces.")
                    .arg(mesh.FN() - beforeFaceCount)
            });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveWrtQ)) {
        if (mesh.VN() <= 0)
            return { false, false, QObject::tr("Current mesh has no vertices.") };

        const float threshold =
            float(doubleParameter(parameters, QStringLiteral("max_quality_thr"), 1.0));
        int deletedVertices = 0;
        int deletedFaces = 0;
        for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
            if (!vi->IsD() && vi->Q() < threshold) {
                vcg::tri::Allocator<VCGMesh>::DeleteVertex(mesh, *vi);
                ++deletedVertices;
            }
        }
        for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
            if (fi->IsD())
                continue;
            if (fi->V(0)->IsD() || fi->V(1)->IsD() || fi->V(2)->IsD()) {
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, *fi);
                ++deletedFaces;
            }
        }

        if (deletedVertices > 0 || deletedFaces > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed low-quality vertices from '%1'").arg(entry.name));
        }

        return successResult(
            deletedVertices > 0 || deletedFaces > 0,
            {
                QObject::tr(
                    "Deleted %1 vertices and %2 faces with quality lower than %3.")
                    .arg(deletedVertices)
                    .arg(deletedFaces)
                    .arg(QString::number(threshold, 'f', 6))
            });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveIsolatedDiameter)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const float minComponentDiag =
            float(doubleParameter(parameters, QStringLiteral("min_component_diag"), mesh.bbox.Diag() / 10.0));
        const auto delInfo =
            vcg::tri::Clean<VCGMesh>::RemoveSmallConnectedComponentsDiameter(mesh, minComponentDiag);

        int delVert = 0;
        if (boolParameter(parameters, QStringLiteral("remove_unref"), true))
            delVert = vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(mesh);

        const bool modified = delInfo.second > 0 || delVert > 0;
        if (modified) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed isolated components by diameter on '%1'").arg(entry.name));
        }
        return successResult(
            modified,
            {
                QObject::tr("Removed %1 connected components out of %2.")
                    .arg(delInfo.second)
                    .arg(delInfo.first),
                QObject::tr("Removed %1 unreferenced vertices.").arg(delVert)
            });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveIsolatedComplexity)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const int minComponentSize =
            std::max(0, intParameter(parameters, QStringLiteral("min_component_size"), 25));
        const auto delInfo =
            vcg::tri::Clean<VCGMesh>::RemoveSmallConnectedComponentsSize(mesh, minComponentSize);

        int delVert = 0;
        if (boolParameter(parameters, QStringLiteral("remove_unref"), true))
            delVert = vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(mesh);

        const bool modified = delInfo.second > 0 || delVert > 0;
        if (modified) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed isolated components by face count on '%1'").arg(entry.name));
        }
        return successResult(
            modified,
            {
                QObject::tr("Removed %1 connected components out of %2.")
                    .arg(delInfo.second)
                    .arg(delInfo.first),
                QObject::tr("Removed %1 unreferenced vertices.").arg(delVert)
            });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveTVertex)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };

        const QString method =
            enumParameter(parameters, QStringLiteral("method"), QStringLiteral("edge_collapse"));
        const float threshold = float(doubleParameter(parameters, QStringLiteral("threshold"), 40.0));
        const bool repeat = boolParameter(parameters, QStringLiteral("repeat"), true);

        int total = 0;
        if (method == QStringLiteral("edge_flip")) {
            if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0
                || vcg::tri::Clean<VCGMesh>::CountNonManifoldVertexFF(mesh) > 0) {
                return { false, false, QObject::tr("Non manifold mesh. Please clean the mesh first.") };
            }
            total = vcg::tri::Clean<VCGMesh>::RemoveTVertexByFlip(mesh, threshold, repeat);
        } else {
            total = vcg::tri::Clean<VCGMesh>::RemoveTVertexByCollapse(mesh, threshold, repeat);
        }

        if (total > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed T-vertices on '%1'").arg(entry.name));
        }
        return successResult(
            total > 0,
            { QObject::tr("Successfully removed %1 t-vertices.").arg(total) });
    }

    if (filterId == QString::fromLatin1(kFilterSnapMismatchedBorder)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const float edgeDistRatio =
            float(doubleParameter(parameters, QStringLiteral("edge_dist_ratio"), 1.0 / 100.0));
        const bool unifyVertices = boolParameter(parameters, QStringLiteral("unify_vertices"), true);

        const SnapBorderResult snapResult = snapMismatchedBorder(mesh, edgeDistRatio, cb);
        if (snapResult.interrupted || doc.isOperationCancelRequested())
            return interruptedResult();

        int mergedVertices = 0;
        if (unifyVertices)
            mergedVertices = vcg::tri::Clean<VCGMesh>::MergeCloseVertex(mesh, 0.0f);

        const bool modified = (snapResult.splitFaces > 0) || (mergedVertices > 0);
        if (modified) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Snapped mismatched borders on '%1'").arg(entry.name));
        }

        return successResult(
            modified,
            {
                QObject::tr("Successfully split %1 faces to snap borders.").arg(snapResult.splitFaces),
                QObject::tr("Merged %1 exact-duplicate vertices after snapping.").arg(mergedVertices)
            });
    }

    if (filterId == QString::fromLatin1(kFilterMergeCloseVertex)) {
        if (mesh.VN() <= 0)
            return { false, false, QObject::tr("Current mesh has no vertices.") };
        const float threshold = float(doubleParameter(parameters, QStringLiteral("threshold"), mesh.bbox.Diag() / 10000.0));
        const int total = vcg::tri::Clean<VCGMesh>::MergeCloseVertex(mesh, threshold);
        if (total > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Merged close vertices on '%1'").arg(entry.name));
        }
        return successResult(
            total > 0,
            { QObject::tr("Successfully merged %1 vertices.").arg(total) });
    }

    if (filterId == QString::fromLatin1(kFilterMergeWedgeTex)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const float mergeThr = float(doubleParameter(parameters, QStringLiteral("merge_thr"), 1.0 / 10000.0));
        vcg::tri::UpdateTopology<VCGMesh>::VertexFace(mesh);
        const int total = vcg::tri::UpdateTexture<VCGMesh>::WedgeTexMergeClose(mesh, mergeThr);
        entry.ioMask |= Mask::IOM_WEDGTEXCOORD;
        if (total > 0) {
            doc.markMeshMaterialChanged(
                current->index,
                QObject::tr("Merged wedge texture coordinates on '%1'").arg(entry.name));
        }
        return successResult(
            total > 0,
            {
                QObject::tr("Successfully merged %1 wedge tex coords with threshold %2.")
                    .arg(total)
                    .arg(QString::number(mergeThr, 'f', 8))
            });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveDuplicateFace)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const int total = vcg::tri::Clean<VCGMesh>::RemoveDuplicateFace(mesh);
        if (total > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed duplicate faces on '%1'").arg(entry.name));
        }
        return successResult(
            total > 0,
            { QObject::tr("Successfully deleted %1 duplicated faces.").arg(total) });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveFoldFace)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const int total = vcg::tri::Clean<VCGMesh>::RemoveFaceFoldByFlip(mesh);
        if (total > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed folded faces on '%1'").arg(entry.name));
        }
        return successResult(
            total > 0,
            { QObject::tr("Successfully flipped %1 folded faces.").arg(total) });
    }

    if (filterId == QString::fromLatin1(kFilterRepairNonManifEdge)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const QString method =
            enumParameter(parameters, QStringLiteral("method"), QStringLiteral("remove_faces"));
        int total = 0;
        QString info;
        if (method == QStringLiteral("split_vertices")) {
            return {
                false,
                false,
                QObject::tr(
                    "Split Vertices mode is not supported by the current mesh container type. "
                    "Use 'Remove Faces' method instead.")
            };
        } else {
            total = vcg::tri::Clean<VCGMesh>::RemoveNonManifoldFace(mesh);
            info = QObject::tr("Successfully removed %1 non-manifold faces.").arg(total);
        }

        if (total > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Repaired non-manifold edges on '%1'").arg(entry.name));
        }
        return successResult(total > 0, { info });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveNonManifVert)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const float vertDispRatio = float(doubleParameter(parameters, QStringLiteral("vert_disp_ratio"), 0.0));
        const int total = vcg::tri::Clean<VCGMesh>::SplitNonManifoldVertex(mesh, vertDispRatio);
        if (total > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Repaired non-manifold vertices on '%1'").arg(entry.name));
        }
        return successResult(
            total > 0,
            { QObject::tr("Successfully split %1 non-manifold vertices.").arg(total) });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveUnrefVertex)) {
        if (mesh.VN() <= 0)
            return { false, false, QObject::tr("Current mesh has no vertices.") };
        const int delVert = vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(mesh);
        if (delVert > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed unreferenced vertices on '%1'").arg(entry.name));
        }
        return successResult(
            delVert > 0,
            { QObject::tr("Removed %1 unreferenced vertices.").arg(delVert) });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveDuplicatedVertex)) {
        if (mesh.VN() <= 0)
            return { false, false, QObject::tr("Current mesh has no vertices.") };
        const int delVert = vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(mesh);
        if (delVert > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed duplicated vertices on '%1'").arg(entry.name));
        }
        return successResult(
            delVert > 0,
            { QObject::tr("Removed %1 duplicated vertices.").arg(delVert) });
    }

    if (filterId == QString::fromLatin1(kFilterRemoveFaceZeroArea)) {
        if (mesh.FN() <= 0)
            return { false, false, QObject::tr("Current mesh has no faces.") };
        const int nullFaces = vcg::tri::Clean<VCGMesh>::RemoveFaceOutOfRangeArea(mesh, 0.0f);
        if (nullFaces > 0) {
            compactAndUpdateGeometry(mesh);
            doc.markMeshGeometryChanged(
                current->index,
                QObject::tr("Removed zero-area faces on '%1'").arg(entry.name));
        }
        return successResult(
            nullFaces > 0,
            { QObject::tr("Removed %1 null faces.").arg(nullFaces) });
    }

    return { false, false, QObject::tr("Unknown filter id: %1").arg(filterId) };
}

void registerCleanFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<CleanFilterPlugin>());
}
