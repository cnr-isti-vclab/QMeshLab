#include "meshingfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#endif
#include <wrap/io_trimesh/io_mask.h>
#include <wrap/gl/glu_tessellator_cap.h>
#include <vcg/container/simple_temporary_data.h>
#include <vcg/complex/allocate.h>
#include <vcg/complex/append.h>
#include <vcg/complex/algorithms/attribute_seam.h>
#include <vcg/complex/algorithms/bitquad_creation.h>
#include <vcg/complex/algorithms/bitquad_support.h>
#include <vcg/complex/algorithms/clean.h>
#include <vcg/complex/algorithms/clustering.h>
#include <vcg/complex/algorithms/create/platonic.h>
#include <vcg/complex/algorithms/hole.h>
#include <vcg/complex/algorithms/intersection.h>
#include <vcg/complex/algorithms/isotropic_remeshing.h>
#include <vcg/complex/algorithms/local_optimization.h>
#include <vcg/complex/algorithms/local_optimization/tri_edge_collapse_quadric.h>
#include <vcg/complex/algorithms/local_optimization/tri_edge_collapse_quadric_tex.h>
#include <vcg/complex/algorithms/pointcloud_normal.h>
#include <vcg/complex/algorithms/polygon_support.h>
#include <vcg/complex/algorithms/refine_catmullclark.h>
#include <vcg/complex/algorithms/refine_doosabin.h>
#include <vcg/complex/algorithms/refine_loop.h>
#include <vcg/complex/algorithms/stat.h>
#include <vcg/complex/algorithms/update/bounding.h>
#include <vcg/complex/algorithms/update/color.h>
#include <vcg/complex/algorithms/update/curvature.h>
#include <vcg/complex/algorithms/update/curvature_fitting.h>
#include <vcg/complex/algorithms/update/flag.h>
#include <vcg/complex/algorithms/update/normal.h>
#include <vcg/complex/algorithms/update/position.h>
#include <vcg/complex/algorithms/update/quality.h>
#include <vcg/complex/algorithms/update/selection.h>
#include <vcg/complex/algorithms/update/topology.h>
#include <vcg/math/base.h>
#include <vcg/space/fitting3.h>
#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace {
// Polygonal mesh used by Doo-Sabin/Catmull-Clark refinement.
class PEdge;
class PFace;
class PVertex;
struct PUsedTypes
    : public vcg::UsedTypes<
          vcg::Use<PVertex>::AsVertexType,
          vcg::Use<PEdge>::AsEdgeType,
          vcg::Use<PFace>::AsFaceType> {};

class PVertex
    : public vcg::Vertex<
          PUsedTypes,
          vcg::vertex::Coord3f,
          vcg::vertex::Normal3f,
          vcg::vertex::Qualityf,
          vcg::vertex::Color4b,
          vcg::vertex::BitFlags> {};

class PEdge
    : public vcg::Edge<
          PUsedTypes,
          vcg::edge::VertexRef,
          vcg::edge::BitFlags> {};

class PFace
    : public vcg::Face<
          PUsedTypes,
          vcg::face::PolyInfo,
          vcg::face::PFVAdj,
          vcg::face::PFFAdj,
          vcg::face::Color4b,
          vcg::face::BitFlags,
          vcg::face::Normal3f,
          vcg::face::WedgeTexCoord2f> {};

class PMesh : public vcg::tri::TriMesh<std::vector<PVertex>, std::vector<PEdge>, std::vector<PFace>> {};

using VertexPair = vcg::tri::BasicVertexPair<VCGVertex>;
using QuadricTemp = vcg::SimpleTempData<VCGMesh::VertContainer, vcg::math::Quadric<double>>;

class QHelper
{
public:
    static void Init() {}
    static vcg::math::Quadric<double> &Qd(VCGVertex &v) { return TD()[v]; }
    static vcg::math::Quadric<double> &Qd(VCGVertex *v) { return TD()[*v]; }
    static VCGVertex::ScalarType W(VCGVertex *) { return 1.0f; }
    static VCGVertex::ScalarType W(VCGVertex &) { return 1.0f; }
    static void Merge(VCGVertex &, const VCGVertex &) {}
    static QuadricTemp *&TDp()
    {
        static QuadricTemp *td = nullptr;
        return td;
    }
    static QuadricTemp &TD() { return *TDp(); }
};

class MyTriEdgeCollapse
    : public vcg::tri::TriEdgeCollapseQuadric<VCGMesh, VertexPair, MyTriEdgeCollapse, QHelper>
{
public:
    using Base = vcg::tri::TriEdgeCollapseQuadric<VCGMesh, VertexPair, MyTriEdgeCollapse, QHelper>;
    MyTriEdgeCollapse(const VertexPair &p, int i, vcg::BaseParameterClass *pp)
        : Base(p, i, pp)
    {
    }
};

class MyTriEdgeCollapseQTex
    : public vcg::tri::TriEdgeCollapseQuadricTex<
          VCGMesh,
          VertexPair,
          MyTriEdgeCollapseQTex,
          vcg::tri::QuadricTexHelper<VCGMesh>>
{
public:
    using Base = vcg::tri::TriEdgeCollapseQuadricTex<
        VCGMesh,
        VertexPair,
        MyTriEdgeCollapseQTex,
        vcg::tri::QuadricTexHelper<VCGMesh>>;
    MyTriEdgeCollapseQTex(const VertexPair &p, int i, vcg::BaseParameterClass *pp)
        : Base(p, i, pp)
    {
    }
};

constexpr QLatin1StringView kIdLoop("meshing_surface_subdivision_loop");
constexpr QLatin1StringView kIdButterfly("meshing_surface_subdivision_butterfly");
constexpr QLatin1StringView kIdClustering("meshing_decimation_clustering");
constexpr QLatin1StringView kIdQuadric("meshing_decimation_quadric_edge_collapse");
constexpr QLatin1StringView kIdQuadricTex("meshing_decimation_quadric_edge_collapse_with_texture");
constexpr QLatin1StringView kIdIsoRemesh("meshing_isotropic_explicit_remeshing");
constexpr QLatin1StringView kIdNormalExtrap("compute_normal_for_point_clouds");
constexpr QLatin1StringView kIdNormalSmoothPc("apply_normal_point_cloud_smoothing");
constexpr QLatin1StringView kIdCurvDir("compute_curvature_principal_directions_per_vertex");
constexpr QLatin1StringView kIdSlicePlane("generate_polyline_from_planar_section");
constexpr QLatin1StringView kIdPerimeterPolyline("generate_polyline_from_selection_perimeter");
constexpr QLatin1StringView kIdMidpoint("meshing_surface_subdivision_midpoint");
constexpr QLatin1StringView kIdReorient("meshing_re_orient_faces_coherently");
constexpr QLatin1StringView kIdFlipSwap("apply_matrix_flip_or_swap_axis");
constexpr QLatin1StringView kIdRotate("compute_matrix_from_rotation");
constexpr QLatin1StringView kIdRotateFit("compute_matrix_by_fitting_to_plane");
constexpr QLatin1StringView kIdScale("compute_matrix_from_scaling_or_normalization");
constexpr QLatin1StringView kIdCenter("compute_matrix_from_translation");
constexpr QLatin1StringView kIdPrincipalAxis("compute_matrix_by_principal_axis");
constexpr QLatin1StringView kIdInvertFaces("meshing_invert_face_orientation");
constexpr QLatin1StringView kIdFreeze("apply_matrix_freeze");
constexpr QLatin1StringView kIdReset("set_matrix_identity");
constexpr QLatin1StringView kIdInvertTr("apply_matrix_inverse");
constexpr QLatin1StringView kIdSetParams("compute_matrix_from_translation_rotation_scale");
constexpr QLatin1StringView kIdSetMatrix("set_matrix");
constexpr QLatin1StringView kIdCloseHoles("meshing_close_holes");
constexpr QLatin1StringView kIdCylinderUnwrap("generate_cylindrical_unwrapping");
constexpr QLatin1StringView kIdCatmull("meshing_surface_subdivision_catmull_clark");
constexpr QLatin1StringView kIdDooSabin("meshing_surface_subdivision_doo_sabin");
constexpr QLatin1StringView kIdHalfCatmull("meshing_tri_to_quad_by_4_8_subdivision");
constexpr QLatin1StringView kIdQuadDominant("meshing_tri_to_quad_dominant");
constexpr QLatin1StringView kIdMakePureTri("meshing_poly_to_tri");
constexpr QLatin1StringView kIdQuadPairing("meshing_tri_to_quad_by_smart_triangle_pairing");
constexpr QLatin1StringView kIdFauxCrease("compute_selection_crease_per_edge");
constexpr QLatin1StringView kIdFauxExtract("generate_polyline_from_selected_edges");
constexpr QLatin1StringView kIdVAttrSeam("meshing_vertex_attribute_seam");
constexpr QLatin1StringView kIdLS3Loop("meshing_surface_subdivision_ls3_loop");

struct TransformOptions {
    bool allLayers = false;
    bool freeze = true;
};

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

QString enumParameter(const MeshFilterParameterValues &params, const QString &id, const QString &fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    const QString value = it.value().toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

void addBoolParam(
    MeshFilterDescriptor &d,
    const QString &id,
    const QString &label,
    const QString &helpMarkdown,
    bool defaultValue,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = helpMarkdown;
    p.group = group;
    p.type = MeshFilterParameterType::Bool;
    p.defaultValue = defaultValue;
    d.parameters.push_back(std::move(p));
}

void addIntParam(
    MeshFilterDescriptor &d,
    const QString &id,
    const QString &label,
    const QString &helpMarkdown,
    int defaultValue,
    int minValue,
    int maxValue,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = helpMarkdown;
    p.group = group;
    p.type = MeshFilterParameterType::Int;
    p.defaultValue = defaultValue;
    p.minValue = minValue;
    p.maxValue = maxValue;
    d.parameters.push_back(std::move(p));
}

void addDoubleParam(
    MeshFilterDescriptor &d,
    const QString &id,
    const QString &label,
    const QString &helpMarkdown,
    double defaultValue,
    double minValue,
    double maxValue,
    int decimals,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = helpMarkdown;
    p.group = group;
    p.type = MeshFilterParameterType::Double;
    p.defaultValue = defaultValue;
    p.minValue = minValue;
    p.maxValue = maxValue;
    p.decimals = decimals;
    d.parameters.push_back(std::move(p));
}

void addEnumParam(
    MeshFilterDescriptor &d,
    const QString &id,
    const QString &label,
    const QString &helpMarkdown,
    const QString &defaultValue,
    std::vector<MeshFilterEnumOption> options,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = helpMarkdown;
    p.group = group;
    p.type = MeshFilterParameterType::Enum;
    p.defaultValue = defaultValue;
    p.enumOptions = std::move(options);
    d.parameters.push_back(std::move(p));
}

int selectedFaceCount(const VCGMesh &mesh)
{
    int cnt = 0;
    for (const VCGFace &f : mesh.face) {
        if (!f.IsD() && f.IsS())
            ++cnt;
    }
    return cnt;
}

int selectedVertCount(const VCGMesh &mesh)
{
    int cnt = 0;
    for (const VCGVertex &v : mesh.vert) {
        if (!v.IsD() && v.IsS())
            ++cnt;
    }
    return cnt;
}

vcg::Box3f sceneBBox(const Document &doc, bool visibleOnly)
{
    vcg::Box3f bb;
    bb.SetNull();
    for (int i = 0; i < doc.meshCount(); ++i) {
        const Document::MeshEntry &entry = doc.mesh(i);
        if (visibleOnly && !entry.visible)
            continue;
        vcg::Box3f mb = entry.mesh.bbox;
        if (mb.IsNull())
            vcg::tri::UpdateBounding<VCGMesh>::Box(const_cast<VCGMesh &>(entry.mesh));
        bb.Add(entry.mesh.bbox);
    }
    return bb;
}

vcg::Point3f transformVectorLinear(const vcg::Matrix44f &m, const vcg::Point3f &v)
{
    return {
        m[0][0] * v.X() + m[0][1] * v.Y() + m[0][2] * v.Z(),
        m[1][0] * v.X() + m[1][1] * v.Y() + m[1][2] * v.Z(),
        m[2][0] * v.X() + m[2][1] * v.Y() + m[2][2] * v.Z()
    };
}

void applyTransformToMesh(VCGMesh &mesh, const vcg::Matrix44f &tr)
{
    for (VCGVertex &v : mesh.vert) {
        if (v.IsD())
            continue;
        v.P() = tr * v.cP();
        vcg::Point3f nn = transformVectorLinear(tr, v.cN());
        const float n2 = nn.SquaredNorm();
        if (n2 > 1e-20f)
            v.N() = nn / std::sqrt(n2);
    }
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    if (mesh.FN() > 0)
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

void applyTransform(
    Document &doc,
    const vcg::Matrix44f &tr,
    const TransformOptions &opt,
    QVector<int> &touched)
{
    touched.clear();
    for (int i = 0; i < doc.meshCount(); ++i) {
        Document::MeshEntry &entry = doc.mesh(i);
        if (opt.allLayers && !entry.visible)
            continue;
        if (!opt.allLayers && i != doc.currentMeshIndex())
            continue;
        applyTransformToMesh(entry.mesh, tr);
        touched.push_back(i);
    }
}

void quadricSimplification(
    VCGMesh &mesh,
    int targetFaceNum,
    bool selected,
    vcg::tri::TriEdgeCollapseQuadricParameter &pp,
    vcg::CallBackPos *cb)
{
    vcg::math::Quadric<double> qZero;
    qZero.SetZero();
    QuadricTemp td(mesh.vert, qZero);
    QHelper::TDp() = &td;

    if (selected) {
        vcg::tri::UpdateSelection<VCGMesh>::VertexFromFaceStrict(mesh);
        for (VCGVertex &v : mesh.vert) {
            if (v.IsD())
                continue;
            if (!v.IsS())
                v.ClearW();
            else
                v.SetW();
        }
    }

    if (pp.PreserveBoundary && !selected) {
        pp.FastPreserveBoundary = true;
        pp.PreserveBoundary = false;
    }

    if (pp.NormalCheck)
        pp.NormalThrRad = float(M_PI / 4.0);

    vcg::LocalOptimization<VCGMesh> deciSession(mesh, &pp);
    if (cb)
        (*cb)(1, "Initializing simplification");
    deciSession.Init<MyTriEdgeCollapse>();

    if (selected)
        targetFaceNum = mesh.fn - (selectedFaceCount(mesh) - targetFaceNum);

    deciSession.SetTargetSimplices(targetFaceNum);
    deciSession.SetTimeBudget(0.1f);
    const int faceToDel = std::max(1, mesh.fn - targetFaceNum);
    while (deciSession.DoOptimization() && mesh.fn > targetFaceNum) {
        if (cb) {
            const int p = 100 - 100 * (mesh.fn - targetFaceNum) / faceToDel;
            if (!(*cb)(p, "Simplifying..."))
                break;
        }
    }

    deciSession.Finalize<MyTriEdgeCollapse>();

    if (selected) {
        for (VCGVertex &v : mesh.vert) {
            if (!v.IsD())
                v.SetW();
            if (v.IsS())
                v.ClearS();
        }
    }
    QHelper::TDp() = nullptr;
}

void quadricTexSimplification(
    VCGMesh &mesh,
    int targetFaceNum,
    bool selected,
    vcg::tri::TriEdgeCollapseQuadricTexParameter &pp,
    vcg::CallBackPos *cb)
{
    vcg::tri::UpdateNormal<VCGMesh>::PerFace(mesh);
    vcg::math::Quadric<double> qZero;
    qZero.SetZero();
    using QTH = vcg::tri::QuadricTexHelper<VCGMesh>;
    QTH::QuadricTemp td3(mesh.vert, qZero);
    QTH::TDp3() = &td3;

    std::vector<std::pair<vcg::TexCoord2<float>, vcg::Quadric5<double>>> qv;
    QTH::Quadric5Temp td(mesh.vert, qv);
    QTH::TDp() = &td;

    if (selected) {
        vcg::tri::UpdateSelection<VCGMesh>::VertexFromFaceStrict(mesh);
        for (VCGVertex &v : mesh.vert) {
            if (v.IsD())
                continue;
            if (!v.IsS())
                v.ClearW();
            else
                v.SetW();
        }
    }

    vcg::LocalOptimization<VCGMesh> deciSession(mesh, &pp);
    if (cb)
        (*cb)(1, "Initializing simplification");
    deciSession.Init<MyTriEdgeCollapseQTex>();

    if (selected)
        targetFaceNum = mesh.fn - (selectedFaceCount(mesh) - targetFaceNum);

    deciSession.SetTargetSimplices(targetFaceNum);
    deciSession.SetTimeBudget(0.1f);
    const int faceToDel = std::max(1, mesh.fn - targetFaceNum);
    while (deciSession.DoOptimization() && mesh.fn > targetFaceNum) {
        if (cb) {
            const int p = 100 - 100 * (mesh.fn - targetFaceNum) / faceToDel;
            if (!(*cb)(p, "Simplifying textured mesh..."))
                break;
        }
    }

    deciSession.Finalize<MyTriEdgeCollapseQTex>();

    if (selected) {
        for (VCGVertex &v : mesh.vert) {
            if (!v.IsD())
                v.SetW();
            if (v.IsS())
                v.ClearS();
        }
    }

    QTH::TDp3() = nullptr;
    QTH::TDp() = nullptr;
}

MeshFilterDescriptor baseDesc(
    const QString &id,
    const QString &name,
    const QString &shortDesc,
    const QString &longDesc,
    MeshFilterInputDomain input = MeshFilterInputDomain::SingleMesh)
{
    MeshFilterDescriptor d;
    d.id = id;
    d.menuPath = QObject::tr("Meshing");
    d.name = name;
    d.shortDescription = shortDesc;
    d.longDescriptionMarkdown = longDesc;
    d.tags = { QStringLiteral("meshing") };
    d.inputDomain = input;
    d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
    return d;
}

void appendMeshingDescriptors(std::vector<MeshFilterDescriptor> &out, const Document &doc)
{
    const int ci = doc.currentMeshIndex();
    const bool hasCur = ci >= 0 && ci < doc.meshCount();
    const double diag = hasCur ? std::max(1e-9, double(doc.mesh(ci).mesh.bbox.Diag())) : 1.0;
    const int faceCount = hasCur ? doc.mesh(ci).mesh.FN() : 0;
    const int selFaces = hasCur ? selectedFaceCount(doc.mesh(ci).mesh) : 0;

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdLoop),
            QObject::tr("Subdivision Surfaces: Loop"),
            QObject::tr("Apply Loop subdivision."),
            QObject::tr("Apply Loop's Subdivision Surface algorithm. It is an approximant refinement method and it works for every triangle and has rules for extraordinary vertices."));
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("LoopWeight"),
            QObject::tr("Weighting Scheme"),
            QObject::tr("Change the weights used. Allows one to optimize some behaviors over others."),
            QStringLiteral("loop"),
            {
                { QStringLiteral("loop"), QObject::tr("Loop"), {} },
                { QStringLiteral("regularity"), QObject::tr("Enhance regularity"), {} },
                { QStringLiteral("continuity"), QObject::tr("Enhance continuity"), {} }
            });
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of times the model is subdivided."), 3, 1, 64);
        addDoubleParam(d, QStringLiteral("Threshold"), QObject::tr("Edge Threshold"), QObject::tr("All edges longer than this threshold are refined. Zero means uniform refinement."), diag * 0.01, 0.0, diag, 6);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Affect only selected faces"), QObject::tr("If selected the filter affects only selected faces."), selFaces > 0);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdButterfly),
            QObject::tr("Subdivision Surfaces: Butterfly Subdivision"),
            QObject::tr("Apply Butterfly subdivision."),
            QObject::tr("Apply Butterfly Subdivision Surface algorithm. It is an interpolated refinement method defined on arbitrary triangular meshes."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of times the model is subdivided."), 3, 1, 64);
        addDoubleParam(d, QStringLiteral("Threshold"), QObject::tr("Edge Threshold"), QObject::tr("All edges longer than this threshold are refined."), diag * 0.01, 0.0, diag, 6);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Affect only selected faces"), QObject::tr("If selected the filter affects only selected faces."), selFaces > 0);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdMidpoint),
            QObject::tr("Subdivision Surfaces: Midpoint"),
            QObject::tr("Apply midpoint subdivision."),
            QObject::tr("Apply a plain subdivision scheme where every edge is split on its midpoint."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of times the model is subdivided."), 3, 1, 64);
        addDoubleParam(d, QStringLiteral("Threshold"), QObject::tr("Edge Threshold"), QObject::tr("All edges longer than this threshold are refined."), diag * 0.01, 0.0, diag, 6);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Affect only selected faces"), QObject::tr("If selected the filter affects only selected faces."), selFaces > 0);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdLS3Loop),
            QObject::tr("Subdivision Surfaces: LS3 Loop"),
            QObject::tr("Apply LS3 Loop subdivision."),
            QObject::tr("Apply LS3 Subdivision Surface algorithm using Loop's weights."));
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("LoopWeight"),
            QObject::tr("Weighting Scheme"),
            QObject::tr("Change the weights used."),
            QStringLiteral("loop"),
            {
                { QStringLiteral("loop"), QObject::tr("Loop"), {} },
                { QStringLiteral("regularity"), QObject::tr("Enhance regularity"), {} },
                { QStringLiteral("continuity"), QObject::tr("Enhance continuity"), {} }
            });
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of times the model is subdivided."), 3, 1, 64);
        addDoubleParam(d, QStringLiteral("Threshold"), QObject::tr("Edge Threshold"), QObject::tr("All edges longer than this threshold are refined."), diag * 0.01, 0.0, diag, 6);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Affect only selected faces"), QObject::tr("If selected the filter affects only selected faces."), selFaces > 0);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdClustering),
            QObject::tr("Simplification: Clustering Decimation"),
            QObject::tr("Simplify mesh by clustering vertices on a grid."),
            QObject::tr("Simplify the mesh by clustering vertices using a uniform grid."));
        addDoubleParam(d, QStringLiteral("Threshold"), QObject::tr("Cell Size"), QObject::tr("Cell size of clustering grid."), diag * 0.01, 0.0, diag, 6);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdQuadric),
            QObject::tr("Simplification: Quadric Edge Collapse Decimation"),
            QObject::tr("Simplify using quadric-based edge-collapse."),
            QObject::tr("Simplify a mesh using a quadric based edge-collapse strategy."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("TargetFaceNum"), QObject::tr("Target number of faces"), QObject::tr("Desired final number of faces."), std::max(1, (selFaces > 0 ? selFaces / 2 : std::max(1, faceCount / 2))), 1, std::max(1, faceCount));
        addDoubleParam(d, QStringLiteral("TargetPerc"), QObject::tr("Percentage reduction (0..1)"), QObject::tr("Desired final size as percentage of initial size."), 0.0, 0.0, 1.0, 6);
        addDoubleParam(d, QStringLiteral("QualityThr"), QObject::tr("Quality threshold"), QObject::tr("Quality threshold for penalizing bad shaped faces."), 0.3, 0.0, 1.0, 4);
        addBoolParam(d, QStringLiteral("PreserveBoundary"), QObject::tr("Preserve Boundary"), QObject::tr("Try to preserve mesh boundaries."), false);
        addDoubleParam(d, QStringLiteral("BoundaryWeight"), QObject::tr("Boundary Preserving Weight"), QObject::tr("Boundary importance during simplification."), 1.0, 0.0, 1e6, 6);
        addBoolParam(d, QStringLiteral("PreserveNormal"), QObject::tr("Preserve Normal"), QObject::tr("Try to avoid face flipping effects."), false);
        addBoolParam(d, QStringLiteral("PreserveTopology"), QObject::tr("Preserve Topology"), QObject::tr("Avoid collapses causing topology changes."), false);
        addBoolParam(d, QStringLiteral("OptimalPlacement"), QObject::tr("Optimal placement"), QObject::tr("Place collapsed vertices minimizing quadric error."), true);
        addBoolParam(d, QStringLiteral("PlanarQuadric"), QObject::tr("Planar Simplification"), QObject::tr("Additional constraints preserving planar areas."), false);
        addDoubleParam(d, QStringLiteral("PlanarWeight"), QObject::tr("Planar Simp. Weight"), QObject::tr("Weight for preserving planar regions."), 0.001, 0.0, 1e6, 6);
        addBoolParam(d, QStringLiteral("QualityWeight"), QObject::tr("Weighted Simplification"), QObject::tr("Use per-vertex quality as weighting factor."), false);
        addBoolParam(d, QStringLiteral("AutoClean"), QObject::tr("Post-simplification cleaning"), QObject::tr("Run cleaning after simplification."), true);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Simplify only selected faces"), QObject::tr("Apply simplification only to selected faces."), selFaces > 0);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdQuadricTex),
            QObject::tr("Simplification: Quadric Edge Collapse Decimation (with texture)"),
            QObject::tr("Simplify textured meshes preserving UVs."),
            QObject::tr("Simplify a textured mesh using a quadric based edge-collapse strategy preserving UV parametrization."));
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireTextureCoordinates = true;
        addIntParam(d, QStringLiteral("TargetFaceNum"), QObject::tr("Target number of faces"), QObject::tr("Desired final number of faces."), std::max(1, (selFaces > 0 ? selFaces / 2 : std::max(1, faceCount / 2))), 1, std::max(1, faceCount));
        addDoubleParam(d, QStringLiteral("TargetPerc"), QObject::tr("Percentage reduction (0..1)"), QObject::tr("Desired final size as percentage of initial size."), 0.0, 0.0, 1.0, 6);
        addDoubleParam(d, QStringLiteral("QualityThr"), QObject::tr("Quality threshold"), QObject::tr("Quality threshold for penalizing bad shaped faces."), 0.3, 0.0, 1.0, 4);
        addDoubleParam(d, QStringLiteral("Extratcoordw"), QObject::tr("Texture Weight"), QObject::tr("Additional weight for extra texture coordinates."), 1.0, 0.0, 1e6, 6);
        addBoolParam(d, QStringLiteral("PreserveBoundary"), QObject::tr("Preserve Boundary"), QObject::tr("Try to preserve mesh boundaries."), false);
        addDoubleParam(d, QStringLiteral("BoundaryWeight"), QObject::tr("Boundary Preserving Weight"), QObject::tr("Boundary importance during simplification."), 1.0, 0.0, 1e6, 6);
        addBoolParam(d, QStringLiteral("OptimalPlacement"), QObject::tr("Optimal placement"), QObject::tr("Place collapsed vertices minimizing quadric error."), true);
        addBoolParam(d, QStringLiteral("PreserveNormal"), QObject::tr("Preserve Normal"), QObject::tr("Try to avoid face flipping effects."), false);
        addBoolParam(d, QStringLiteral("PlanarQuadric"), QObject::tr("Planar Simplification"), QObject::tr("Additional constraints preserving planar areas."), false);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Simplify only selected faces"), QObject::tr("Apply simplification only to selected faces."), selFaces > 0);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(
            QString::fromLatin1(kIdIsoRemesh),
            QObject::tr("Remeshing: Isotropic Explicit Remeshing"),
            QObject::tr("Explicit isotropic remeshing by local operations."),
            QObject::tr("Perform explicit remeshing by repeatedly applying edge flip, collapse, relax and refine operations."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of remeshing iterations."), 10, 1, 1000);
        addBoolParam(d, QStringLiteral("Adaptive"), QObject::tr("Adaptive remeshing"), QObject::tr("Toggle adaptive isotropic remeshing."), false);
        addBoolParam(d, QStringLiteral("SelectedOnly"), QObject::tr("Remesh only selected faces"), QObject::tr("Apply remeshing only to selected faces."), false);
        addDoubleParam(d, QStringLiteral("TargetLen"), QObject::tr("Target Length"), QObject::tr("Target length for remeshed edges."), diag * 0.01, 0.0, diag, 6);
        addDoubleParam(d, QStringLiteral("FeatureDeg"), QObject::tr("Crease Angle"), QObject::tr("Minimum angle to treat an edge as feature."), 30.0, 0.0, 180.0, 3);
        addBoolParam(d, QStringLiteral("CheckSurfDist"), QObject::tr("Check Surface Distance"), QObject::tr("Each operation must satisfy max surface distance."), false);
        addDoubleParam(d, QStringLiteral("MaxSurfDist"), QObject::tr("Max. Surface Distance"), QObject::tr("Maximum allowed local surface deviation."), diag * 0.01, 0.0, diag, 6);
        addBoolParam(d, QStringLiteral("SplitFlag"), QObject::tr("Refine Step"), QObject::tr("Include refine step."), true);
        addBoolParam(d, QStringLiteral("CollapseFlag"), QObject::tr("Collapse Step"), QObject::tr("Include collapse step."), true);
        addBoolParam(d, QStringLiteral("SwapFlag"), QObject::tr("Edge-Swap Step"), QObject::tr("Include edge-swap step."), true);
        addBoolParam(d, QStringLiteral("SmoothFlag"), QObject::tr("Smooth Step"), QObject::tr("Include smoothing step."), true);
        addBoolParam(d, QStringLiteral("ReprojectFlag"), QObject::tr("Reproject Step"), QObject::tr("Include projection step."), true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdReorient), QObject::tr("Re-Orient all faces coherently"), QObject::tr("Orient faces consistently."), QObject::tr("Re-orient in a consistent way all faces of the mesh."));
        d.inputRequirements.requireFaces = true;
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdInvertFaces), QObject::tr("Invert Faces Orientation"), QObject::tr("Flip mesh face orientation."), QObject::tr("Invert faces orientation, flipping mesh normals."));
        d.inputRequirements.requireFaces = true;
        addBoolParam(d, QStringLiteral("forceFlip"), QObject::tr("Force Flip"), QObject::tr("Always flip normals; otherwise try to set normals outside."), true);
        addBoolParam(d, QStringLiteral("onlySelected"), QObject::tr("Flip only selected faces"), QObject::tr("If selected, only selected faces are affected."), false);
        out.push_back(std::move(d));
    }

    auto addTransformCommon = [](MeshFilterDescriptor &d, bool withFreeze = true) {
        if (withFreeze)
            addBoolParam(d, QStringLiteral("Freeze"), QObject::tr("Freeze Matrix"), QObject::tr("Transformation is explicitly applied to vertices."), true);
        addBoolParam(d, QStringLiteral("allLayers"), QObject::tr("Apply to all visible Layers"), QObject::tr("Apply to all visible mesh layers."), false);
    };

    {
        auto d = baseDesc(QString::fromLatin1(kIdFlipSwap), QObject::tr("Transform: Flip and/or swap axis"), QObject::tr("Flip or swap axes."), QObject::tr("Generate a transformation that flips and/or swaps axes."));
        addBoolParam(d, QStringLiteral("flipX"), QObject::tr("Flip X axis"), QObject::tr("Mirror along YZ plane."), false);
        addBoolParam(d, QStringLiteral("flipY"), QObject::tr("Flip Y axis"), QObject::tr("Mirror along XZ plane."), false);
        addBoolParam(d, QStringLiteral("flipZ"), QObject::tr("Flip Z axis"), QObject::tr("Mirror along XY plane."), false);
        addBoolParam(d, QStringLiteral("swapXY"), QObject::tr("Swap X-Y axis"), QObject::tr("Swap X and Y."), false);
        addBoolParam(d, QStringLiteral("swapXZ"), QObject::tr("Swap X-Z axis"), QObject::tr("Swap X and Z."), false);
        addBoolParam(d, QStringLiteral("swapYZ"), QObject::tr("Swap Y-Z axis"), QObject::tr("Swap Y and Z."), false);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdRotate), QObject::tr("Transform: Rotate"), QObject::tr("Rotate mesh."), QObject::tr("Generate a matrix transformation that rotates the mesh."));
        addEnumParam(
            d,
            QStringLiteral("rotAxis"),
            QObject::tr("Rotation on"),
            QObject::tr("Choose rotation axis."),
            QStringLiteral("x"),
            {
                { QStringLiteral("x"), QObject::tr("X axis"), {} },
                { QStringLiteral("y"), QObject::tr("Y axis"), {} },
                { QStringLiteral("z"), QObject::tr("Z axis"), {} },
                { QStringLiteral("custom"), QObject::tr("Custom axis"), {} }
            });
        addEnumParam(
            d,
            QStringLiteral("rotCenter"),
            QObject::tr("Center of rotation"),
            QObject::tr("Choose center of rotation."),
            QStringLiteral("origin"),
            {
                { QStringLiteral("origin"), QObject::tr("origin"), {} },
                { QStringLiteral("barycenter"), QObject::tr("barycenter"), {} },
                { QStringLiteral("custom"), QObject::tr("custom point"), {} }
            });
        addDoubleParam(d, QStringLiteral("angle"), QObject::tr("Rotation Angle"), QObject::tr("Angle in degrees."), 0.0, -360.0, 360.0, 3);
        addDoubleParam(d, QStringLiteral("customAxisX"), QObject::tr("Custom axis X"), QObject::tr("X component."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customAxisY"), QObject::tr("Custom axis Y"), QObject::tr("Y component."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customAxisZ"), QObject::tr("Custom axis Z"), QObject::tr("Z component."), 1.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customCenterX"), QObject::tr("Custom center X"), QObject::tr("X center."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customCenterY"), QObject::tr("Custom center Y"), QObject::tr("Y center."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customCenterZ"), QObject::tr("Custom center Z"), QObject::tr("Z center."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addBoolParam(d, QStringLiteral("snapFlag"), QObject::tr("Snap angle"), QObject::tr("Snap angle according to snap value."), false);
        addDoubleParam(d, QStringLiteral("snapAngle"), QObject::tr("Snapping Value"), QObject::tr("Snap step in degrees."), 30.0, 0.0001, 360.0, 4);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdRotateFit), QObject::tr("Transform: Rotate to Fit to a plane"), QObject::tr("Rotate selection to fit a reference plane."), QObject::tr("Generate a transformation rotating selection to fit XY/YZ/ZX plane."));
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("targetPlane"),
            QObject::tr("Rotate to fit"),
            QObject::tr("Target plane."),
            QStringLiteral("xy"),
            {
                { QStringLiteral("xy"), QObject::tr("XY plane"), {} },
                { QStringLiteral("yz"), QObject::tr("YZ plane"), {} },
                { QStringLiteral("zx"), QObject::tr("ZX plane"), {} }
            });
        addEnumParam(
            d,
            QStringLiteral("rotAxis"),
            QObject::tr("Rotate on"),
            QObject::tr("Rotation axis constraint."),
            QStringLiteral("any"),
            {
                { QStringLiteral("any"), QObject::tr("any axis"), {} },
                { QStringLiteral("x"), QObject::tr("X axis"), {} },
                { QStringLiteral("y"), QObject::tr("Y axis"), {} },
                { QStringLiteral("z"), QObject::tr("Z axis"), {} }
            });
        addBoolParam(d, QStringLiteral("ToOrigin"), QObject::tr("Move to Origin"), QObject::tr("Translate so selection centroid rests on origin."), true);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdPrincipalAxis), QObject::tr("Transform: Align to Principal Axis"), QObject::tr("Align mesh to principal axis."), QObject::tr("Generate a transformation aligning mesh to principal inertia axis."));
        addBoolParam(d, QStringLiteral("pointsFlag"), QObject::tr("Use vertex"), QObject::tr("Use vertices only (suitable for point clouds/open meshes)."), true);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdScale), QObject::tr("Transform: Scale, Normalize"), QObject::tr("Scale mesh."), QObject::tr("Generate a scaling transformation; optionally normalize to unit bounding box."));
        addDoubleParam(d, QStringLiteral("axisX"), QObject::tr("X Axis"), QObject::tr("Scale X."), 1.0, -1e6, 1e6, 6);
        addDoubleParam(d, QStringLiteral("axisY"), QObject::tr("Y Axis"), QObject::tr("Scale Y."), 1.0, -1e6, 1e6, 6);
        addDoubleParam(d, QStringLiteral("axisZ"), QObject::tr("Z Axis"), QObject::tr("Scale Z."), 1.0, -1e6, 1e6, 6);
        addBoolParam(d, QStringLiteral("uniformFlag"), QObject::tr("Uniform Scaling"), QObject::tr("Use same scale for all axes (axisX value)."), true);
        addEnumParam(
            d,
            QStringLiteral("scaleCenter"),
            QObject::tr("Center of scaling"),
            QObject::tr("Scaling center."),
            QStringLiteral("origin"),
            {
                { QStringLiteral("origin"), QObject::tr("origin"), {} },
                { QStringLiteral("barycenter"), QObject::tr("barycenter"), {} },
                { QStringLiteral("custom"), QObject::tr("custom point"), {} }
            });
        addDoubleParam(d, QStringLiteral("customCenterX"), QObject::tr("Custom center X"), QObject::tr("X center."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customCenterY"), QObject::tr("Custom center Y"), QObject::tr("Y center."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customCenterZ"), QObject::tr("Custom center Z"), QObject::tr("Z center."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addBoolParam(d, QStringLiteral("unitFlag"), QObject::tr("Scale to Unit bbox"), QObject::tr("Scale object to unit bounding box."), false);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdCenter), QObject::tr("Transform: Translate, Center, set Origin"), QObject::tr("Translate mesh."), QObject::tr("Generate translation matrix by offsets or centering modes."));
        addEnumParam(
            d,
            QStringLiteral("traslMethod"),
            QObject::tr("Transformation"),
            QObject::tr("Translation strategy."),
            QStringLiteral("xyz"),
            {
                { QStringLiteral("xyz"), QObject::tr("XYZ translation"), {} },
                { QStringLiteral("scene_bbox"), QObject::tr("Center on Scene BBox"), {} },
                { QStringLiteral("layer_bbox"), QObject::tr("Center on Layer BBox"), {} },
                { QStringLiteral("new_origin"), QObject::tr("Set new Origin"), {} }
            });
        addDoubleParam(d, QStringLiteral("axisX"), QObject::tr("X Axis"), QObject::tr("Translation X."), 0.0, -5.0 * diag, 5.0 * diag, 6);
        addDoubleParam(d, QStringLiteral("axisY"), QObject::tr("Y Axis"), QObject::tr("Translation Y."), 0.0, -5.0 * diag, 5.0 * diag, 6);
        addDoubleParam(d, QStringLiteral("axisZ"), QObject::tr("Z Axis"), QObject::tr("Translation Z."), 0.0, -5.0 * diag, 5.0 * diag, 6);
        addDoubleParam(d, QStringLiteral("newOriginX"), QObject::tr("New Origin X"), QObject::tr("X of new origin."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("newOriginY"), QObject::tr("New Origin Y"), QObject::tr("Y of new origin."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("newOriginZ"), QObject::tr("New Origin Z"), QObject::tr("Z of new origin."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdReset), QObject::tr("Matrix: Reset Current Matrix"), QObject::tr("Reset transform matrix to identity."), QObject::tr("Set the current transformation matrix to identity."));
        addTransformCommon(d, false);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdFreeze), QObject::tr("Matrix: Freeze Current Matrix"), QObject::tr("Apply current matrix to geometry."), QObject::tr("Freeze current transformation matrix into vertex coordinates."));
        addTransformCommon(d, false);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdInvertTr), QObject::tr("Matrix: Invert Current Matrix"), QObject::tr("Invert current transformation matrix."), QObject::tr("Invert the current transformation matrix."));
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdSetParams), QObject::tr("Matrix: Set from translation/rotation/scale"), QObject::tr("Build transformation from T/R/S parameters."), QObject::tr("Set transformation matrix from translation, Euler rotation and scale parameters."));
        addDoubleParam(d, QStringLiteral("translationX"), QObject::tr("X Translation"), QObject::tr("Translation X."), 0.0, -1e9, 1e9, 6);
        addDoubleParam(d, QStringLiteral("translationY"), QObject::tr("Y Translation"), QObject::tr("Translation Y."), 0.0, -1e9, 1e9, 6);
        addDoubleParam(d, QStringLiteral("translationZ"), QObject::tr("Z Translation"), QObject::tr("Translation Z."), 0.0, -1e9, 1e9, 6);
        addDoubleParam(d, QStringLiteral("rotationX"), QObject::tr("X Rotation"), QObject::tr("Euler rotation X (deg)."), 0.0, -360.0, 360.0, 4);
        addDoubleParam(d, QStringLiteral("rotationY"), QObject::tr("Y Rotation"), QObject::tr("Euler rotation Y (deg)."), 0.0, -360.0, 360.0, 4);
        addDoubleParam(d, QStringLiteral("rotationZ"), QObject::tr("Z Rotation"), QObject::tr("Euler rotation Z (deg)."), 0.0, -360.0, 360.0, 4);
        addDoubleParam(d, QStringLiteral("scaleX"), QObject::tr("X Scale"), QObject::tr("Scale X."), 1.0, -1e6, 1e6, 6);
        addDoubleParam(d, QStringLiteral("scaleY"), QObject::tr("Y Scale"), QObject::tr("Scale Y."), 1.0, -1e6, 1e6, 6);
        addDoubleParam(d, QStringLiteral("scaleZ"), QObject::tr("Z Scale"), QObject::tr("Scale Z."), 1.0, -1e6, 1e6, 6);
        addBoolParam(d, QStringLiteral("compose"), QObject::tr("Compose with current"), QObject::tr("Compose with current matrix."), false);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdSetMatrix), QObject::tr("Matrix: Set/Copy Transformation"), QObject::tr("Set transformation matrix values."), QObject::tr("Set current transformation matrix by filling matrix coefficients."));
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                const QString pid = QStringLiteral("m%1%2").arg(r).arg(c);
                const double def = (r == c) ? 1.0 : 0.0;
                addDoubleParam(d, pid, QObject::tr("m%1%2").arg(r).arg(c), QObject::tr("Matrix coefficient."), def, -1e9, 1e9, 6, QStringLiteral("advanced.matrix"));
            }
        }
        addBoolParam(d, QStringLiteral("compose"), QObject::tr("Compose with current"), QObject::tr("Compose with current matrix."), false);
        addTransformCommon(d, true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdNormalExtrap), QObject::tr("Compute normals for point sets"), QObject::tr("Estimate normals for point clouds."), QObject::tr("Compute normals of vertices without relying on triangle connectivity."));
        addIntParam(d, QStringLiteral("K"), QObject::tr("Neighbour num"), QObject::tr("Number of neighbors used to estimate normals."), 10, 1, 100000);
        addIntParam(d, QStringLiteral("smoothIter"), QObject::tr("Smooth Iteration"), QObject::tr("Number of smoothing iterations."), 0, 0, 1000);
        addBoolParam(d, QStringLiteral("flipFlag"), QObject::tr("Flip normals w.r.t viewpoint"), QObject::tr("Use viewpoint to orient normals consistently."), false);
        addDoubleParam(d, QStringLiteral("viewPosX"), QObject::tr("Viewpoint X"), QObject::tr("Viewpoint X."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("viewPosY"), QObject::tr("Viewpoint Y"), QObject::tr("Viewpoint Y."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("viewPosZ"), QObject::tr("Viewpoint Z"), QObject::tr("Viewpoint Z."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdNormalSmoothPc), QObject::tr("Smooth normals on point sets"), QObject::tr("Smooth point-cloud normals."), QObject::tr("Smooth vertex normals on point sets."));
        addIntParam(d, QStringLiteral("K"), QObject::tr("Number of neighbors"), QObject::tr("Number of neighbors used to smooth normals."), 10, 1, 100000);
        addBoolParam(d, QStringLiteral("useDist"), QObject::tr("Weight using neighbour distance"), QObject::tr("Weight neighbor normals according to distance."), false);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdCurvDir), QObject::tr("Compute curvature principal directions"), QObject::tr("Compute principal curvature directions."), QObject::tr("Compute principal directions of curvature with different algorithms."));
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("Method"),
            QObject::tr("Method"),
            QObject::tr("Choose method."),
            QStringLiteral("quadric_fitting"),
            {
                { QStringLiteral("taubin"), QObject::tr("Taubin approximation"), {} },
                { QStringLiteral("pca"), QObject::tr("Principal Component Analysis"), {} },
                { QStringLiteral("normal_cycle"), QObject::tr("Normal Cycles"), {} },
                { QStringLiteral("quadric_fitting"), QObject::tr("Quadric Fitting"), {} },
                { QStringLiteral("sd_quadric"), QObject::tr("Scale Dependent Quadric Fitting"), {} }
            });
        addEnumParam(
            d,
            QStringLiteral("CurvColorMethod"),
            QObject::tr("Quality/Color Mapping"),
            QObject::tr("Choose curvature mapped to quality/color."),
            QStringLiteral("mean"),
            {
                { QStringLiteral("mean"), QObject::tr("Mean Curvature"), {} },
                { QStringLiteral("gaussian"), QObject::tr("Gaussian Curvature"), {} },
                { QStringLiteral("min"), QObject::tr("Min Curvature"), {} },
                { QStringLiteral("max"), QObject::tr("Max Curvature"), {} },
                { QStringLiteral("shape"), QObject::tr("Shape Index"), {} },
                { QStringLiteral("curvedness"), QObject::tr("Curvedness"), {} },
                { QStringLiteral("none"), QObject::tr("None"), {} }
            });
        addDoubleParam(d, QStringLiteral("Scale"), QObject::tr("Curvature Scale"), QObject::tr("Scale for scale-dependent methods."), diag * 0.1, 0.0, diag, 6);
        addBoolParam(d, QStringLiteral("Autoclean"), QObject::tr("Remove Unreferenced Vertices"), QObject::tr("Remove unreferenced vertices before computing."), true);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdCloseHoles), QObject::tr("Close Holes"), QObject::tr("Close holes under a size threshold."), QObject::tr("Close holes whose boundary has less edges than threshold."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("MaxHoleSize"), QObject::tr("Max size to be closed"), QObject::tr("Hole size threshold in boundary-edge count."), 30, 1, 1000000);
        addBoolParam(d, QStringLiteral("Selected"), QObject::tr("Close holes with selected faces"), QObject::tr("Only holes with selected boundary faces are closed."), false);
        addBoolParam(d, QStringLiteral("NewFaceSelected"), QObject::tr("Select newly created faces"), QObject::tr("Leave newly created faces selected."), true);
        addBoolParam(d, QStringLiteral("SelfIntersection"), QObject::tr("Prevent self intersecting faces"), QObject::tr("Try to avoid creating self-intersecting faces."), true);
        addBoolParam(d, QStringLiteral("RefineHole"), QObject::tr("Refine Filled Hole"), QObject::tr("Refine newly created hole triangles."), false);
        addDoubleParam(d, QStringLiteral("RefineHoleEdgeLen"), QObject::tr("Hole Refinement Edge Len"), QObject::tr("Target edge length for hole refinement."), diag * 0.03, 0.0, diag, 6);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdCylinderUnwrap), QObject::tr("Geometric Cylindrical Unwrapping"), QObject::tr("Unwrap geometry along cylindrical projection."), QObject::tr("Unwrap geometry of current mesh along a cylindrical projection."));
        d.outputDomain = MeshFilterOutputDomain::NewMeshes;
        d.inputRequirements.requireFaces = true;
        addDoubleParam(d, QStringLiteral("startAngle"), QObject::tr("Start angle (deg)"), QObject::tr("Starting angle of unrolling."), 0.0, -1e6, 1e6, 4);
        addDoubleParam(d, QStringLiteral("endAngle"), QObject::tr("End angle (deg)"), QObject::tr("Ending angle of unrolling."), 360.0, -1e6, 1e6, 4);
        addDoubleParam(d, QStringLiteral("radius"), QObject::tr("Projection Radius"), QObject::tr("Reference cylinder radius. 0 = auto."), 0.0, 0.0, 1e9, 6);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdCatmull), QObject::tr("Subdivision Surfaces: Catmull-Clark"), QObject::tr("Apply Catmull-Clark subdivision."), QObject::tr("Apply Catmull-Clark Subdivision Surfaces."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of times model is subdivided."), 2, 1, 16);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdHalfCatmull), QObject::tr("Tri to Quad by 4-8 Subdivision"), QObject::tr("Convert tri mesh into quad mesh by 4-8 subdivision."), QObject::tr("Convert a tri mesh into a quad mesh by applying 4-8 subdivision."));
        d.inputRequirements.requireFaces = true;
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdDooSabin), QObject::tr("Subdivision Surfaces: Doo Sabin"), QObject::tr("Apply Doo-Sabin subdivision."), QObject::tr("Apply Doo-Sabin Subdivision Surfaces."));
        d.inputRequirements.requireFaces = true;
        addIntParam(d, QStringLiteral("Iterations"), QObject::tr("Iterations"), QObject::tr("Number of times model is subdivided."), 2, 1, 16);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdQuadDominant), QObject::tr("Turn into Quad-Dominant mesh"), QObject::tr("Convert tri mesh to quad-dominant mesh."), QObject::tr("Convert tri-mesh to quad-dominant mesh by pairing suitable triangles."));
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("level"),
            QObject::tr("Optimize For"),
            QObject::tr("Greedy strategy."),
            QStringLiteral("fewest"),
            {
                { QStringLiteral("fewest"), QObject::tr("Fewest triangles"), {} },
                { QStringLiteral("mid"), QObject::tr("(in between)"), {} },
                { QStringLiteral("shape"), QObject::tr("Better quad shape"), {} }
            });
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdMakePureTri), QObject::tr("Turn into a Pure-Triangular mesh"), QObject::tr("Split any polygonal face into triangles."), QObject::tr("Convert into tri-mesh by splitting polygonal faces."));
        d.inputRequirements.requireFaces = true;
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdQuadPairing), QObject::tr("Tri to Quad by smart triangle pairing"), QObject::tr("Pair triangles into quads."), QObject::tr("Convert tri-mesh into quad mesh by pairing triangles."));
        d.inputRequirements.requireFaces = true;
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdFauxCrease), QObject::tr("Select Crease Edges"), QObject::tr("Select crease edges from dihedral angles."), QObject::tr("Select crease edges according to signed dihedral angle."));
        d.inputRequirements.requireFaces = true;
        addDoubleParam(d, QStringLiteral("AngleDegNeg"), QObject::tr("Concave Angle Thr. (deg)"), QObject::tr("Concave dihedral threshold."), -45.0, -180.0, 180.0, 4);
        addDoubleParam(d, QStringLiteral("AngleDegPos"), QObject::tr("Convex Angle Thr. (deg)"), QObject::tr("Convex dihedral threshold."), 45.0, -180.0, 180.0, 4);
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdFauxExtract), QObject::tr("Build a Polyline from Selected Edges"), QObject::tr("Create edge mesh from selected edges."), QObject::tr("Create a new layer with edge mesh composed only by selected edges."));
        d.outputDomain = MeshFilterOutputDomain::NewMeshes;
        d.inputRequirements.requireFaces = true;
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdVAttrSeam), QObject::tr("Vertex Attribute Seam"), QObject::tr("Split vertices to make attributes seam-independent."), QObject::tr("Make selected vertex attributes connectivity-independent by splitting vertices."));
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("NormalMode"),
            QObject::tr("Normal Source"),
            QObject::tr("Choose normal source."),
            QStringLiteral("none"),
            {
                { QStringLiteral("none"), QObject::tr("None"), {} },
                { QStringLiteral("vertex"), QObject::tr("Vertex"), {} },
                { QStringLiteral("face"), QObject::tr("Face"), {} }
            });
        addEnumParam(
            d,
            QStringLiteral("ColorMode"),
            QObject::tr("Color Source"),
            QObject::tr("Choose color source."),
            QStringLiteral("none"),
            {
                { QStringLiteral("none"), QObject::tr("None"), {} },
                { QStringLiteral("vertex"), QObject::tr("Vertex"), {} },
                { QStringLiteral("face"), QObject::tr("Face"), {} }
            });
        addEnumParam(
            d,
            QStringLiteral("TexcoordMode"),
            QObject::tr("Texcoord Source"),
            QObject::tr("Choose texcoord source."),
            QStringLiteral("none"),
            {
                { QStringLiteral("none"), QObject::tr("None"), {} },
                { QStringLiteral("vertex"), QObject::tr("Vertex"), {} },
                { QStringLiteral("wedge"), QObject::tr("Wedge"), {} }
            });
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdPerimeterPolyline), QObject::tr("Create Selection Perimeter Polyline"), QObject::tr("Create polyline from selection perimeter."), QObject::tr("Create a new layer with edge mesh composed by selection perimeter edges."));
        d.outputDomain = MeshFilterOutputDomain::NewMeshes;
        d.inputRequirements.requireFaces = true;
        out.push_back(std::move(d));
    }

    {
        auto d = baseDesc(QString::fromLatin1(kIdSlicePlane), QObject::tr("Compute Planar Section"), QObject::tr("Compute planar section polyline."), QObject::tr("Compute polyline representing planar section of a mesh."));
        d.outputDomain = MeshFilterOutputDomain::NewMeshes;
        d.inputRequirements.requireFaces = true;
        addEnumParam(
            d,
            QStringLiteral("planeAxis"),
            QObject::tr("Plane perpendicular to"),
            QObject::tr("Slicing plane normal axis."),
            QStringLiteral("x"),
            {
                { QStringLiteral("x"), QObject::tr("X Axis"), {} },
                { QStringLiteral("y"), QObject::tr("Y Axis"), {} },
                { QStringLiteral("z"), QObject::tr("Z Axis"), {} },
                { QStringLiteral("custom"), QObject::tr("Custom Axis"), {} }
            });
        addDoubleParam(d, QStringLiteral("customAxisX"), QObject::tr("Custom axis X"), QObject::tr("Custom axis X."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customAxisY"), QObject::tr("Custom axis Y"), QObject::tr("Custom axis Y."), 1.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("customAxisZ"), QObject::tr("Custom axis Z"), QObject::tr("Custom axis Z."), 0.0, -1e9, 1e9, 6, QStringLiteral("advanced"));
        addDoubleParam(d, QStringLiteral("planeOffset"), QObject::tr("Cross plane offset"), QObject::tr("Offset from reference point."), 0.0, -1e6, 1e6, 6);
        addEnumParam(
            d,
            QStringLiteral("relativeTo"),
            QObject::tr("Plane reference"),
            QObject::tr("Reference frame for plane offset."),
            QStringLiteral("origin"),
            {
                { QStringLiteral("center"), QObject::tr("Bounding box center"), {} },
                { QStringLiteral("min"), QObject::tr("Bounding box min"), {} },
                { QStringLiteral("origin"), QObject::tr("Origin"), {} }
            });
        addBoolParam(d, QStringLiteral("createSectionSurface"), QObject::tr("Create also section surface"), QObject::tr("Create triangulated section if polyline is closed."), false);
        addBoolParam(d, QStringLiteral("splitSurfaceWithSection"), QObject::tr("Create also split surfaces"), QObject::tr("Create under/over split layers (requires manifold mesh)."), false);
        out.push_back(std::move(d));
    }
}

MeshFilterRunResult fail(const QString &msg)
{
    MeshFilterRunResult r;
    r.success = false;
    r.documentModified = false;
    r.errorMessage = msg;
    return r;
}

MeshFilterRunResult success(bool modified, const QStringList &info = {}, const QVector<int> &newMeshes = {})
{
    MeshFilterRunResult r;
    r.success = true;
    r.documentModified = modified;
    r.infoMessages = info;
    r.newMeshIndices = newMeshes;
    return r;
}

} // namespace

QString MeshingFilterPlugin::pluginId() const
{
    return QStringLiteral("qmeshlab.filter.meshing");
}

QString MeshingFilterPlugin::name() const
{
    return QObject::tr("QMeshLab Meshing Filters");
}

std::vector<MeshFilterDescriptor> MeshingFilterPlugin::filters(const Document &doc) const
{
    std::vector<MeshFilterDescriptor> out;
    out.reserve(40);
    appendMeshingDescriptors(out, doc);
    return out;
}

MeshFilterRunResult MeshingFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
    Document &doc) const
{
    const int ci = doc.currentMeshIndex();
    if (ci < 0 || ci >= doc.meshCount())
        return fail(QObject::tr("No current mesh selected."));

    auto &entry = doc.mesh(ci);
    auto &mesh = entry.mesh;
    using Mask = vcg::tri::io::Mask;

    auto markGeometry = [&](int idx, const QString &msg) {
        doc.markMeshGeometryChanged(idx, msg);
    };

    try {
        if (filterId == QString::fromLatin1(kIdLoop)
            || filterId == QString::fromLatin1(kIdButterfly)
            || filterId == QString::fromLatin1(kIdMidpoint)
            || filterId == QString::fromLatin1(kIdLS3Loop)) {
            if (mesh.FN() <= 0)
                return fail(QObject::tr("Current mesh has no faces."));

            vcg::tri::Allocator<VCGMesh>::CompactFaceVector(mesh);
            vcg::tri::Allocator<VCGMesh>::CompactVertexVector(mesh);
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromFF(mesh);
            if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0) {
                return fail(QObject::tr("Subdivision surfaces require manifoldness."));
            }

            const bool selected = boolParameter(parameters, QStringLiteral("Selected"), false);
            const float threshold = float(doubleParameter(parameters, QStringLiteral("Threshold"), 0.0));
            const int iterations = std::max(1, intParameter(parameters, QStringLiteral("Iterations"), 3));
            const QString w = enumParameter(parameters, QStringLiteral("LoopWeight"), QStringLiteral("loop"));
            vcg::CallBackPos *cb = doc.progressCallback();

            for (int i = 0; i < iterations; ++i) {
                if (filterId == QString::fromLatin1(kIdLoop)) {
                    if (w == QStringLiteral("regularity")) {
                        vcg::tri::RefineOddEven<VCGMesh>(
                            mesh,
                            vcg::tri::OddPointLoopGeneric<VCGMesh, vcg::tri::Centroid<VCGMesh>, vcg::tri::RegularLoopWeight<float>>(mesh),
                            vcg::tri::EvenPointLoopGeneric<VCGMesh, vcg::tri::Centroid<VCGMesh>, vcg::tri::RegularLoopWeight<float>>(),
                            threshold,
                            selected,
                            cb);
                    } else if (w == QStringLiteral("continuity")) {
                        vcg::tri::RefineOddEven<VCGMesh>(
                            mesh,
                            vcg::tri::OddPointLoopGeneric<VCGMesh, vcg::tri::Centroid<VCGMesh>, vcg::tri::ContinuityLoopWeight<float>>(mesh),
                            vcg::tri::EvenPointLoopGeneric<VCGMesh, vcg::tri::Centroid<VCGMesh>, vcg::tri::ContinuityLoopWeight<float>>(),
                            threshold,
                            selected,
                            cb);
                    } else {
                        vcg::tri::RefineOddEven<VCGMesh>(
                            mesh,
                            vcg::tri::OddPointLoop<VCGMesh>(mesh),
                            vcg::tri::EvenPointLoop<VCGMesh>(),
                            threshold,
                            selected,
                            cb);
                    }
                } else if (filterId == QString::fromLatin1(kIdButterfly)) {
                    vcg::tri::Refine<VCGMesh, vcg::tri::MidPointButterfly<VCGMesh>>(
                        mesh,
                        vcg::tri::MidPointButterfly<VCGMesh>(mesh),
                        threshold,
                        selected,
                        cb);
                } else if (filterId == QString::fromLatin1(kIdMidpoint)) {
                    vcg::tri::Refine<VCGMesh, vcg::tri::MidPoint<VCGMesh>>(
                        mesh,
                        vcg::tri::MidPoint<VCGMesh>(&mesh),
                        threshold,
                        selected,
                        cb);
                } else {
                    if (w == QStringLiteral("regularity")) {
                        vcg::tri::RefineOddEven<VCGMesh>(
                            mesh,
                            vcg::tri::OddPointLoopGeneric<VCGMesh, vcg::tri::LS3Projection<VCGMesh, double>, vcg::tri::RegularLoopWeight<double>>(mesh),
                            vcg::tri::EvenPointLoopGeneric<VCGMesh, vcg::tri::LS3Projection<VCGMesh, double>, vcg::tri::RegularLoopWeight<double>>(),
                            threshold,
                            selected,
                            cb);
                    } else if (w == QStringLiteral("continuity")) {
                        vcg::tri::RefineOddEven<VCGMesh>(
                            mesh,
                            vcg::tri::OddPointLoopGeneric<VCGMesh, vcg::tri::LS3Projection<VCGMesh, double>, vcg::tri::ContinuityLoopWeight<double>>(mesh),
                            vcg::tri::EvenPointLoopGeneric<VCGMesh, vcg::tri::LS3Projection<VCGMesh, double>, vcg::tri::ContinuityLoopWeight<double>>(),
                            threshold,
                            selected,
                            cb);
                    } else {
                        vcg::tri::RefineOddEven<VCGMesh>(
                            mesh,
                            vcg::tri::OddPointLoopGeneric<VCGMesh, vcg::tri::LS3Projection<VCGMesh, double>>(mesh),
                            vcg::tri::EvenPointLoopGeneric<VCGMesh, vcg::tri::LS3Projection<VCGMesh, double>>(),
                            threshold,
                            selected,
                            cb);
                    }
                }
            }
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            markGeometry(ci, QObject::tr("Applied subdivision on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdReorient)) {
            if (mesh.FN() <= 0)
                return fail(QObject::tr("Current mesh has no faces."));
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0)
                return fail(QObject::tr("Orientability requires manifoldness."));
            bool oriented = false;
            bool orientable = false;
            vcg::tri::Clean<VCGMesh>::OrientCoherentlyMesh(mesh, oriented, orientable);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            markGeometry(ci, QObject::tr("Reoriented faces on '%1'").arg(entry.name));
            return success(true, { QObject::tr("Oriented: %1, Orientable: %2").arg(oriented).arg(orientable) });
        }

        if (filterId == QString::fromLatin1(kIdClustering)) {
            const float threshold = float(doubleParameter(parameters, QStringLiteral("Threshold"), mesh.bbox.Diag() * 0.01));
            vcg::tri::Clustering<VCGMesh, vcg::tri::AverageColorCell<VCGMesh>> grid(mesh.bbox, 100000, threshold);
            if (mesh.FN() == 0) {
                grid.AddPointSet(mesh);
                grid.ExtractPointSet(mesh);
            } else {
                grid.AddMesh(mesh);
                grid.ExtractMesh(mesh);
            }
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            markGeometry(ci, QObject::tr("Applied clustering decimation on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdInvertFaces)) {
            const bool forceFlip = boolParameter(parameters, QStringLiteral("forceFlip"), true);
            const bool onlySel = boolParameter(parameters, QStringLiteral("onlySelected"), false);
            if (forceFlip)
                vcg::tri::Clean<VCGMesh>::FlipMesh(mesh, onlySel);
            else
                vcg::tri::Clean<VCGMesh>::FlipNormalOutside(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            markGeometry(ci, QObject::tr("Inverted face orientation on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdQuadric)) {
            vcg::tri::UpdateTopology<VCGMesh>::VertexFace(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromVF(mesh);

            int targetFaceNum = intParameter(parameters, QStringLiteral("TargetFaceNum"), std::max(1, mesh.FN() / 2));
            const float targetPerc = float(doubleParameter(parameters, QStringLiteral("TargetPerc"), 0.0));
            if (targetPerc > 0.0f)
                targetFaceNum = int(std::round(mesh.FN() * targetPerc));
            targetFaceNum = std::clamp(targetFaceNum, 1, std::max(1, mesh.FN()));

            vcg::tri::TriEdgeCollapseQuadricParameter pp;
            pp.QualityThr = float(doubleParameter(parameters, QStringLiteral("QualityThr"), 0.3));
            pp.PreserveBoundary = boolParameter(parameters, QStringLiteral("PreserveBoundary"), false);
            pp.BoundaryQuadricWeight = pp.BoundaryQuadricWeight * float(doubleParameter(parameters, QStringLiteral("BoundaryWeight"), 1.0));
            pp.PreserveTopology = boolParameter(parameters, QStringLiteral("PreserveTopology"), false);
            pp.QualityWeight = boolParameter(parameters, QStringLiteral("QualityWeight"), false);
            pp.NormalCheck = boolParameter(parameters, QStringLiteral("PreserveNormal"), false);
            pp.OptimalPlacement = boolParameter(parameters, QStringLiteral("OptimalPlacement"), true);
            pp.QualityQuadric = boolParameter(parameters, QStringLiteral("PlanarQuadric"), false);
            pp.QualityQuadricWeight = float(doubleParameter(parameters, QStringLiteral("PlanarWeight"), pp.QualityQuadricWeight));
            const bool selected = boolParameter(parameters, QStringLiteral("Selected"), false);

            quadricSimplification(mesh, targetFaceNum, selected, pp, doc.progressCallback());

            if (boolParameter(parameters, QStringLiteral("AutoClean"), true)) {
                vcg::tri::Clean<VCGMesh>::RemoveFaceOutOfRangeArea(mesh, 0);
                vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(mesh);
                vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(mesh);
                vcg::tri::Allocator<VCGMesh>::CompactVertexVector(mesh);
                vcg::tri::Allocator<VCGMesh>::CompactFaceVector(mesh);
            }

            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::NormalizePerVertex(mesh);
            markGeometry(ci, QObject::tr("Applied quadric simplification on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdQuadricTex)) {
            vcg::tri::UpdateTopology<VCGMesh>::VertexFace(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceBorderFromVF(mesh);

            if (!vcg::tri::Clean<VCGMesh>::HasConsistentPerWedgeTexCoord(mesh))
                return fail(QObject::tr("Mesh has inconsistent per-wedge texture coordinates."));

            int targetFaceNum = intParameter(parameters, QStringLiteral("TargetFaceNum"), std::max(1, mesh.FN() / 2));
            const float targetPerc = float(doubleParameter(parameters, QStringLiteral("TargetPerc"), 0.0));
            if (targetPerc > 0.0f)
                targetFaceNum = int(std::round(mesh.FN() * targetPerc));
            targetFaceNum = std::clamp(targetFaceNum, 1, std::max(1, mesh.FN()));

            vcg::tri::TriEdgeCollapseQuadricTexParameter pp;
            pp.QualityThr = float(doubleParameter(parameters, QStringLiteral("QualityThr"), 0.3));
            pp.ExtraTCoordWeight = float(doubleParameter(parameters, QStringLiteral("Extratcoordw"), 1.0));
            pp.OptimalPlacement = boolParameter(parameters, QStringLiteral("OptimalPlacement"), true);
            pp.PreserveBoundary = boolParameter(parameters, QStringLiteral("PreserveBoundary"), false);
            pp.BoundaryWeight = pp.BoundaryWeight * float(doubleParameter(parameters, QStringLiteral("BoundaryWeight"), 1.0));
            pp.QualityQuadric = boolParameter(parameters, QStringLiteral("PlanarQuadric"), false);
            pp.NormalCheck = boolParameter(parameters, QStringLiteral("PreserveNormal"), false);
            const bool selected = boolParameter(parameters, QStringLiteral("Selected"), false);

            quadricTexSimplification(mesh, targetFaceNum, selected, pp, doc.progressCallback());
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::NormalizePerVertex(mesh);
            markGeometry(ci, QObject::tr("Applied textured quadric simplification on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdIsoRemesh)) {
            vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(mesh);
            vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(mesh);
            vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
            vcg::tri::UpdateFlags<VCGMesh>::FaceClearF(mesh); // remove faux edges
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);

            VCGMesh toProjectCopy;
            vcg::tri::Append<VCGMesh, VCGMesh>::Mesh(toProjectCopy, mesh);
            vcg::tri::IsotropicRemeshing<VCGMesh>::Params params;
            params.SetTargetLen(float(doubleParameter(parameters, QStringLiteral("TargetLen"), mesh.bbox.Diag() * 0.01)));
            params.SetFeatureAngleDeg(float(doubleParameter(parameters, QStringLiteral("FeatureDeg"), 30.0)));
            params.maxSurfDist = float(doubleParameter(parameters, QStringLiteral("MaxSurfDist"), mesh.bbox.Diag() * 0.01));
            params.iter = std::max(1, intParameter(parameters, QStringLiteral("Iterations"), 10));
            params.adapt = boolParameter(parameters, QStringLiteral("Adaptive"), false);
            params.selectedOnly = boolParameter(parameters, QStringLiteral("SelectedOnly"), false);
            params.splitFlag = boolParameter(parameters, QStringLiteral("SplitFlag"), true);
            params.collapseFlag = boolParameter(parameters, QStringLiteral("CollapseFlag"), true);
            params.swapFlag = boolParameter(parameters, QStringLiteral("SwapFlag"), true);
            params.smoothFlag = boolParameter(parameters, QStringLiteral("SmoothFlag"), true);
            params.projectFlag = boolParameter(parameters, QStringLiteral("ReprojectFlag"), true);
            params.surfDistCheck = boolParameter(parameters, QStringLiteral("CheckSurfDist"), false);

            vcg::tri::IsotropicRemeshing<VCGMesh>::Do(mesh, toProjectCopy, params, doc.progressCallback());
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            markGeometry(ci, QObject::tr("Applied isotropic remeshing on '%1'").arg(entry.name));
            return success(true);
        }

        auto makeTransformOptions = [&]() {
            TransformOptions o;
            o.allLayers = boolParameter(parameters, QStringLiteral("allLayers"), false);
            o.freeze = boolParameter(parameters, QStringLiteral("Freeze"), true);
            return o;
        };

        auto applyTransformAndMark = [&](const vcg::Matrix44f &tr, const TransformOptions &opt, const QString &opName) {
            QVector<int> touched;
            applyTransform(doc, tr, opt, touched);
            for (int idx : touched)
                markGeometry(idx, QObject::tr("%1 on '%2'").arg(opName, doc.mesh(idx).name));
            return success(!touched.isEmpty(), { QObject::tr("Affected layers: %1").arg(touched.size()) });
        };

        if (filterId == QString::fromLatin1(kIdFlipSwap)) {
            vcg::Matrix44f tr;
            tr.SetIdentity();
            if (boolParameter(parameters, QStringLiteral("flipX"), false)) {
                vcg::Matrix44f m; m.SetIdentity(); m[0][0] = -1.0f; tr *= m;
            }
            if (boolParameter(parameters, QStringLiteral("flipY"), false)) {
                vcg::Matrix44f m; m.SetIdentity(); m[1][1] = -1.0f; tr *= m;
            }
            if (boolParameter(parameters, QStringLiteral("flipZ"), false)) {
                vcg::Matrix44f m; m.SetIdentity(); m[2][2] = -1.0f; tr *= m;
            }
            if (boolParameter(parameters, QStringLiteral("swapXY"), false)) {
                vcg::Matrix44f m; m.SetIdentity(); m[0][0] = 0; m[0][1] = 1; m[1][0] = 1; m[1][1] = 0; tr *= m;
            }
            if (boolParameter(parameters, QStringLiteral("swapXZ"), false)) {
                vcg::Matrix44f m; m.SetIdentity(); m[0][0] = 0; m[0][2] = 1; m[2][0] = 1; m[2][2] = 0; tr *= m;
            }
            if (boolParameter(parameters, QStringLiteral("swapYZ"), false)) {
                vcg::Matrix44f m; m.SetIdentity(); m[1][1] = 0; m[1][2] = 1; m[2][1] = 1; m[2][2] = 0; tr *= m;
            }
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Flip/Swap axes"));
        }

        if (filterId == QString::fromLatin1(kIdRotate)) {
            const QString axisMode = enumParameter(parameters, QStringLiteral("rotAxis"), QStringLiteral("x"));
            vcg::Point3f axis(1, 0, 0);
            if (axisMode == QStringLiteral("y"))
                axis = { 0, 1, 0 };
            else if (axisMode == QStringLiteral("z"))
                axis = { 0, 0, 1 };
            else if (axisMode == QStringLiteral("custom"))
                axis = {
                    float(doubleParameter(parameters, QStringLiteral("customAxisX"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customAxisY"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customAxisZ"), 1.0))
                };

            const float n2 = axis.SquaredNorm();
            if (n2 <= 1e-20f)
                return fail(QObject::tr("Custom rotation axis must be non-zero."));
            axis /= std::sqrt(n2);

            vcg::Point3f center(0, 0, 0);
            const QString centerMode = enumParameter(parameters, QStringLiteral("rotCenter"), QStringLiteral("origin"));
            if (centerMode == QStringLiteral("barycenter"))
                center = mesh.bbox.Center();
            else if (centerMode == QStringLiteral("custom"))
                center = {
                    float(doubleParameter(parameters, QStringLiteral("customCenterX"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customCenterY"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customCenterZ"), 0.0))
                };

            float angleDeg = float(doubleParameter(parameters, QStringLiteral("angle"), 0.0));
            if (boolParameter(parameters, QStringLiteral("snapFlag"), false)) {
                const float snap = float(doubleParameter(parameters, QStringLiteral("snapAngle"), 30.0));
                if (std::fabs(snap) > 1e-12f)
                    angleDeg = std::floor(angleDeg / snap) * snap;
            }

            vcg::Matrix44f trRot;
            trRot.SetRotateDeg(angleDeg, axis);
            vcg::Matrix44f trT, trInvT;
            trT.SetTranslate(center);
            trInvT.SetTranslate(-center);
            vcg::Matrix44f tr = trT * trRot * trInvT;
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Rotate"));
        }

        if (filterId == QString::fromLatin1(kIdRotateFit)) {
            if (selectedVertCount(mesh) == 0 && selectedFaceCount(mesh) == 0)
                return fail(QObject::tr("Cannot compute rotation: there is no selection."));

            if (selectedVertCount(mesh) == 0 && selectedFaceCount(mesh) > 0) {
                vcg::tri::UpdateSelection<VCGMesh>::VertexClear(mesh);
                vcg::tri::UpdateSelection<VCGMesh>::VertexFromFaceLoose(mesh);
            }

            vcg::Box3f selBox;
            selBox.SetNull();
            std::vector<vcg::Point3f> selectedPts;
            selectedPts.reserve(static_cast<size_t>(std::max(1, selectedVertCount(mesh))));
            for (VCGVertex &v : mesh.vert) {
                if (v.IsD() || !v.IsS())
                    continue;
                selBox.Add(v.P());
                selectedPts.push_back(v.P());
            }
            if (selectedPts.empty())
                return fail(QObject::tr("Cannot compute rotation: empty selected vertices."));

            vcg::Plane3f plane;
            vcg::FitPlaneToPointSet(selectedPts, plane);

            vcg::Point3f targetPlane(0, 0, 1);
            const QString tplane = enumParameter(parameters, QStringLiteral("targetPlane"), QStringLiteral("xy"));
            if (tplane == QStringLiteral("yz"))
                targetPlane = { 1, 0, 0 };
            else if (tplane == QStringLiteral("zx"))
                targetPlane = { 0, 1, 0 };

            vcg::Point3f rotAxis = targetPlane ^ plane.Direction();
            float angleRad = vcg::Angle(targetPlane, plane.Direction());

            const QString raxis = enumParameter(parameters, QStringLiteral("rotAxis"), QStringLiteral("any"));
            if (raxis != QStringLiteral("any")) {
                vcg::Point3f projDir;
                if (raxis == QStringLiteral("x")) {
                    rotAxis = -vcg::Point3f(1, 0, 0);
                    projDir = { 0, plane.Direction().Y(), plane.Direction().Z() };
                } else if (raxis == QStringLiteral("y")) {
                    rotAxis = -vcg::Point3f(0, 1, 0);
                    projDir = { plane.Direction().X(), 0, plane.Direction().Z() };
                } else {
                    rotAxis = -vcg::Point3f(0, 0, 1);
                    projDir = { plane.Direction().X(), plane.Direction().Y(), 0 };
                }
                angleRad = vcg::Angle(targetPlane, projDir);
                const float angleSign = (targetPlane ^ projDir) * rotAxis;
                if (angleSign < 0)
                    angleRad = -angleRad;
                else if (angleSign == 0)
                    angleRad = 0;
            }

            const float rn2 = rotAxis.SquaredNorm();
            if (rn2 <= 1e-20f)
                return fail(QObject::tr("Cannot compute fitting rotation axis."));
            rotAxis /= std::sqrt(rn2);

            vcg::Matrix44f rt;
            rt.SetRotateRad(-angleRad, rotAxis);
            vcg::Matrix44f tr = rt;
            if (boolParameter(parameters, QStringLiteral("ToOrigin"), true)) {
                vcg::Matrix44f t;
                t.SetTranslate(-selBox.Center());
                tr = rt * t;
            }
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Rotate to fit"));
        }

        if (filterId == QString::fromLatin1(kIdPrincipalAxis)) {
            const bool pointsFlag = boolParameter(parameters, QStringLiteral("pointsFlag"), true);
            (void) pointsFlag;

            std::vector<vcg::Point3f> pts;
            pts.reserve(static_cast<size_t>(mesh.VN()));
            vcg::Point3f bp(0, 0, 0);
            for (const VCGVertex &v : mesh.vert) {
                if (v.IsD())
                    continue;
                pts.push_back(v.cP());
                bp += v.cP();
            }
            if (pts.empty())
                return fail(QObject::tr("Current mesh has no vertices."));
            bp /= float(pts.size());

            Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
            for (const vcg::Point3f &p : pts) {
                const Eigen::Vector3d d(double(p.X() - bp.X()), double(p.Y() - bp.Y()), double(p.Z() - bp.Z()));
                cov += d * d.transpose();
            }
            cov /= double(std::max<size_t>(1, pts.size()));
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(cov);
            if (eig.info() != Eigen::Success)
                return fail(QObject::tr("Failed to compute principal-axis eigen decomposition."));

            Eigen::Matrix3d ev = eig.eigenvectors();
            vcg::Matrix44f tr;
            tr.SetIdentity();
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    tr[r][c] = float(ev(c, r));
            tr.transposeInPlace();
            if (tr.Determinant() < 0)
                for (int i = 0; i < 3; ++i)
                    tr[2][i] = -tr[2][i];

            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Align to principal axis"));
        }

        if (filterId == QString::fromLatin1(kIdCenter)) {
            vcg::Point3f translation(
                float(doubleParameter(parameters, QStringLiteral("axisX"), 0.0)),
                float(doubleParameter(parameters, QStringLiteral("axisY"), 0.0)),
                float(doubleParameter(parameters, QStringLiteral("axisZ"), 0.0)));

            const QString method = enumParameter(parameters, QStringLiteral("traslMethod"), QStringLiteral("xyz"));
            if (method == QStringLiteral("scene_bbox"))
                translation = -sceneBBox(doc, true).Center();
            else if (method == QStringLiteral("layer_bbox"))
                translation = -mesh.bbox.Center();
            else if (method == QStringLiteral("new_origin"))
                translation = -vcg::Point3f(
                    float(doubleParameter(parameters, QStringLiteral("newOriginX"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("newOriginY"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("newOriginZ"), 0.0)));

            vcg::Matrix44f tr;
            tr.SetTranslate(translation);
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Translate/Center"));
        }

        if (filterId == QString::fromLatin1(kIdScale)) {
            vcg::Box3f sb = boolParameter(parameters, QStringLiteral("allLayers"), false)
                ? sceneBBox(doc, true)
                : mesh.bbox;

            float sx = float(doubleParameter(parameters, QStringLiteral("axisX"), 1.0));
            float sy = float(doubleParameter(parameters, QStringLiteral("axisY"), 1.0));
            float sz = float(doubleParameter(parameters, QStringLiteral("axisZ"), 1.0));
            if (boolParameter(parameters, QStringLiteral("uniformFlag"), true))
                sy = sz = sx;
            if (boolParameter(parameters, QStringLiteral("unitFlag"), false)) {
                const float maxSide = std::max(sb.DimX(), std::max(sb.DimY(), sb.DimZ()));
                if (maxSide > 1e-20f)
                    sx = sy = sz = 1.0f / maxSide;
            }

            vcg::Point3f c(0, 0, 0);
            const QString centerMode = enumParameter(parameters, QStringLiteral("scaleCenter"), QStringLiteral("origin"));
            if (centerMode == QStringLiteral("barycenter"))
                c = sb.Center();
            else if (centerMode == QStringLiteral("custom"))
                c = {
                    float(doubleParameter(parameters, QStringLiteral("customCenterX"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customCenterY"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customCenterZ"), 0.0))
                };

            vcg::Matrix44f s;
            s.SetScale(sx, sy, sz);
            vcg::Matrix44f t, it;
            t.SetTranslate(c);
            it.SetTranslate(-c);
            vcg::Matrix44f tr = t * s * it;
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Scale"));
        }

        if (filterId == QString::fromLatin1(kIdReset)
            || filterId == QString::fromLatin1(kIdFreeze)
            || filterId == QString::fromLatin1(kIdInvertTr)) {
            // QMeshLab currently has no persistent per-layer transform matrix. Treat these
            // as successful no-op operations for compatibility with MeshLab filter set.
            return success(false, { QObject::tr("No persistent transform matrix in QMeshLab: no-op.") });
        }

        if (filterId == QString::fromLatin1(kIdSetParams)) {
            const float tx = float(doubleParameter(parameters, QStringLiteral("translationX"), 0.0));
            const float ty = float(doubleParameter(parameters, QStringLiteral("translationY"), 0.0));
            const float tz = float(doubleParameter(parameters, QStringLiteral("translationZ"), 0.0));
            const float rx = float(doubleParameter(parameters, QStringLiteral("rotationX"), 0.0));
            const float ry = float(doubleParameter(parameters, QStringLiteral("rotationY"), 0.0));
            const float rz = float(doubleParameter(parameters, QStringLiteral("rotationZ"), 0.0));
            const float sx = float(doubleParameter(parameters, QStringLiteral("scaleX"), 1.0));
            const float sy = float(doubleParameter(parameters, QStringLiteral("scaleY"), 1.0));
            const float sz = float(doubleParameter(parameters, QStringLiteral("scaleZ"), 1.0));

            vcg::Matrix44f tr;
            tr.SetIdentity();
            vcg::Matrix44f tt;
            tt.SetTranslate(tx, ty, tz);
            tr = tr * tt;
            if (rx != 0.0f || ry != 0.0f || rz != 0.0f) {
                tt.FromEulerAngles(vcg::math::ToRad(rx), vcg::math::ToRad(ry), vcg::math::ToRad(rz));
                tr = tr * tt;
            }
            if (sx != 0.0f || sy != 0.0f || sz != 0.0f) {
                tt.SetScale(sx, sy, sz);
                tr = tr * tt;
            }
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Set transform from parameters"));
        }

        if (filterId == QString::fromLatin1(kIdSetMatrix)) {
            vcg::Matrix44f tr;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    tr[r][c] = float(doubleParameter(parameters, QStringLiteral("m%1%2").arg(r).arg(c), (r == c) ? 1.0 : 0.0));
            return applyTransformAndMark(tr, makeTransformOptions(), QObject::tr("Set transform matrix"));
        }

        if (filterId == QString::fromLatin1(kIdNormalExtrap)) {
            vcg::tri::PointCloudNormal<VCGMesh>::Param p;
            p.fittingAdjNum = std::max(1, intParameter(parameters, QStringLiteral("K"), 10));
            p.smoothingIterNum = std::max(0, intParameter(parameters, QStringLiteral("smoothIter"), 0));
            p.useViewPoint = boolParameter(parameters, QStringLiteral("flipFlag"), false);
            p.viewPoint = {
                float(doubleParameter(parameters, QStringLiteral("viewPosX"), 0.0)),
                float(doubleParameter(parameters, QStringLiteral("viewPosY"), 0.0)),
                float(doubleParameter(parameters, QStringLiteral("viewPosZ"), 0.0))
            };
            vcg::tri::PointCloudNormal<VCGMesh>::Compute(mesh, p, doc.progressCallback());
            entry.ioMask |= Mask::IOM_VERTNORMAL;
            doc.markMeshMaterialChanged(ci, QObject::tr("Computed point-set normals for '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdNormalSmoothPc)) {
            vcg::tri::Allocator<VCGMesh>::CompactVertexVector(mesh);
            vcg::tri::Smooth<VCGMesh>::VertexNormalPointCloud(mesh, std::max(1, intParameter(parameters, QStringLiteral("K"), 10)), 1);
            entry.ioMask |= Mask::IOM_VERTNORMAL;
            doc.markMeshMaterialChanged(ci, QObject::tr("Smoothed point-set normals for '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdCurvDir)) {
            const float scale = float(doubleParameter(parameters, QStringLiteral("Scale"), mesh.bbox.Diag() * 0.1));
            if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0)
                return fail(QObject::tr("Cannot compute principal directions on non-manifold faces."));

            vcg::tri::UpdateNormal<VCGMesh>::NormalizePerVertex(mesh);
            if (boolParameter(parameters, QStringLiteral("Autoclean"), true)) {
                vcg::tri::Clean<VCGMesh>::RemoveUnreferencedVertex(mesh);
                vcg::tri::Allocator<VCGMesh>::CompactVertexVector(mesh);
            }

            const QString method = enumParameter(parameters, QStringLiteral("Method"), QStringLiteral("quadric_fitting"));
            if (method == QStringLiteral("taubin")) {
                vcg::tri::UpdateCurvature<VCGMesh>::PrincipalDirections(mesh);
            } else if (method == QStringLiteral("pca")) {
                vcg::tri::UpdateCurvature<VCGMesh>::PrincipalDirectionsPCA(mesh, scale, true, doc.progressCallback());
            } else if (method == QStringLiteral("normal_cycle")) {
                vcg::tri::UpdateCurvature<VCGMesh>::PrincipalDirectionsNormalCycle(mesh);
            } else if (method == QStringLiteral("sd_quadric")) {
                vcg::tri::UpdateCurvatureFitting<VCGMesh>::updateCurvatureLocal(mesh, scale, doc.progressCallback());
            } else {
                vcg::tri::UpdateCurvatureFitting<VCGMesh>::computeCurvature(mesh);
            }

            const QString cm = enumParameter(parameters, QStringLiteral("CurvColorMethod"), QStringLiteral("mean"));
            if (cm == QStringLiteral("gaussian"))
                vcg::tri::UpdateQuality<VCGMesh>::VertexGaussianFromCurvatureDir(mesh);
            else if (cm == QStringLiteral("min"))
                vcg::tri::UpdateQuality<VCGMesh>::VertexMinCurvFromCurvatureDir(mesh);
            else if (cm == QStringLiteral("max"))
                vcg::tri::UpdateQuality<VCGMesh>::VertexMaxCurvFromCurvatureDir(mesh);
            else if (cm == QStringLiteral("shape"))
                vcg::tri::UpdateQuality<VCGMesh>::VertexShapeIndexFromCurvatureDir(mesh);
            else if (cm == QStringLiteral("curvedness"))
                vcg::tri::UpdateQuality<VCGMesh>::VertexCurvednessFromCurvatureDir(mesh);
            else if (cm == QStringLiteral("none"))
                vcg::tri::UpdateQuality<VCGMesh>::VertexConstant(mesh, 0);
            else
                vcg::tri::UpdateQuality<VCGMesh>::VertexMeanFromCurvatureDir(mesh);

            vcg::Histogram<float> h;
            vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityHistogram(mesh, h);
            vcg::tri::UpdateColor<VCGMesh>::PerVertexQualityRamp(mesh, h.Percentile(0.1f), h.Percentile(0.9f));
            entry.ioMask |= Mask::IOM_VERTCOLOR | Mask::IOM_VERTQUALITY;
            doc.markMeshMaterialChanged(ci, QObject::tr("Computed principal curvature directions for '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdCloseHoles)) {
            if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0)
                return fail(QObject::tr("Hole closing requires edge-manifold mesh."));

            const size_t originalSize = mesh.face.size();
            const int maxHoleSize = std::max(1, intParameter(parameters, QStringLiteral("MaxHoleSize"), 30));
            const bool selectedFlag = boolParameter(parameters, QStringLiteral("Selected"), false);
            const bool selfInter = boolParameter(parameters, QStringLiteral("SelfIntersection"), true);
            const bool newFaceSel = boolParameter(parameters, QStringLiteral("NewFaceSelected"), true);
            const bool refineHole = boolParameter(parameters, QStringLiteral("RefineHole"), false);
            const float refineLen = float(doubleParameter(parameters, QStringLiteral("RefineHoleEdgeLen"), mesh.bbox.Diag() * 0.03));

            int holeCnt = 0;
            if (selfInter)
                holeCnt = vcg::tri::Hole<VCGMesh>::EarCuttingIntersectionFill<vcg::tri::SelfIntersectionEar<VCGMesh>>(mesh, maxHoleSize, selectedFlag, doc.progressCallback());
            else
                holeCnt = vcg::tri::Hole<VCGMesh>::EarCuttingFill<vcg::tri::MinimumWeightEar<VCGMesh>>(mesh, maxHoleSize, selectedFlag, doc.progressCallback());

            if (newFaceSel) {
                vcg::tri::UpdateSelection<VCGMesh>::FaceClear(mesh);
                for (size_t i = originalSize; i < mesh.face.size(); ++i) {
                    if (!mesh.face[i].IsD())
                        mesh.face[i].SetS();
                }
            }

            if (refineHole) {
                vcg::tri::IsotropicRemeshing<VCGMesh>::Params params;
                params.SetFeatureAngleDeg(181.0f);
                params.adapt = false;
                params.selectedOnly = true;
                params.splitFlag = true;
                params.collapseFlag = true;
                params.swapFlag = true;
                params.smoothFlag = true;
                params.projectFlag = false;
                params.surfDistCheck = false;
                for (int k = 0; k < 3; ++k) {
                    params.SetTargetLen(refineLen * 3.0f);
                    params.iter = 5;
                    vcg::tri::IsotropicRemeshing<VCGMesh>::Do(mesh, params);

                    params.SetTargetLen(refineLen / 3.0f);
                    params.iter = 3;
                    vcg::tri::IsotropicRemeshing<VCGMesh>::Do(mesh, params);

                    params.SetTargetLen(refineLen);
                    params.iter = 2;
                    vcg::tri::IsotropicRemeshing<VCGMesh>::Do(mesh, params);
                }
            }

            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            markGeometry(ci, QObject::tr("Closed holes on '%1'").arg(entry.name));
            return success(
                true,
                {
                    QObject::tr("Closed %1 holes and added %2 new faces.").arg(holeCnt).arg(mesh.FN() - int(originalSize))
                });
        }

        if (filterId == QString::fromLatin1(kIdCylinderUnwrap)) {
            const float startAngle = float(doubleParameter(parameters, QStringLiteral("startAngle"), 0.0));
            const float endAngle = float(doubleParameter(parameters, QStringLiteral("endAngle"), 360.0));
            const float radiusUser = float(doubleParameter(parameters, QStringLiteral("radius"), 0.0));

            if (endAngle <= startAngle)
                return fail(QObject::tr("End angle must be greater than start angle."));

            const int numLoop = int(1 + (endAngle - startAngle) / 360.0f);
            if (numLoop <= 0)
                return fail(QObject::tr("Invalid unwrapping angular interval."));

            std::vector<std::vector<int>> vertRefLoop(static_cast<size_t>(numLoop));
            for (int i = 0; i < numLoop; ++i)
                vertRefLoop[static_cast<size_t>(i)].assign(mesh.vert.size(), -1);

            VCGMesh unrolled;
            unrolled.textures = mesh.textures;
            float avgR = 0.0f;
            int avgCount = 0;

            for (auto vi = mesh.vert.begin(); vi != mesh.vert.end(); ++vi) {
                if (vi->IsD())
                    continue;
                vcg::Point3f p = vi->P();
                p.Y() = 0;
                VCGMesh::ScalarType ro, theta, phi;
                p.ToPolarRad(ro, theta, phi);
                float thetaDeg = vcg::math::ToDeg(theta);
                int loopIndex = 0;
                while (thetaDeg < endAngle && loopIndex < numLoop) {
                    if (thetaDeg >= startAngle) {
                        auto nvi = vcg::tri::Allocator<VCGMesh>::AddVertices(unrolled, 1);
                        vertRefLoop[static_cast<size_t>(loopIndex)][static_cast<size_t>(vi - mesh.vert.begin())] = int(nvi - unrolled.vert.begin());
                        nvi->ImportData(*vi);
                        nvi->P().X() = -vcg::math::ToRad(thetaDeg);
                        nvi->P().Y() = vi->P().Y();
                        nvi->P().Z() = ro;
                        avgR += ro;
                        ++avgCount;
                    }
                    thetaDeg += 360.0f;
                    ++loopIndex;
                }
            }

            if (avgCount == 0)
                return fail(QObject::tr("Cylindrical unwrapping produced no vertices."));
            avgR = avgR / float(avgCount);
            if (radiusUser > 0.0f)
                avgR = radiusUser;
            for (VCGVertex &v : unrolled.vert)
                v.P().X() *= avgR;

            for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
                if (fi->IsD())
                    continue;
                int loopIndex = 0;
                while (loopIndex < numLoop) {
                    const int endIt = std::min(2, numLoop - loopIndex);
                    for (int ii0 = 0; ii0 < endIt; ++ii0) {
                        for (int ii1 = 0; ii1 < endIt; ++ii1) {
                            for (int ii2 = 0; ii2 < endIt; ++ii2) {
                                const int i0 = vertRefLoop[static_cast<size_t>(loopIndex + ii0)][static_cast<size_t>(fi->V(0) - &mesh.vert[0])];
                                const int i1 = vertRefLoop[static_cast<size_t>(loopIndex + ii1)][static_cast<size_t>(fi->V(1) - &mesh.vert[0])];
                                const int i2 = vertRefLoop[static_cast<size_t>(loopIndex + ii2)][static_cast<size_t>(fi->V(2) - &mesh.vert[0])];
                                if (i0 < 0 || i1 < 0 || i2 < 0)
                                    continue;
                                if (vcg::Distance(unrolled.vert[static_cast<size_t>(i0)].P(), unrolled.vert[static_cast<size_t>(i1)].P()) >= avgR / 10.0f)
                                    continue;
                                if (vcg::Distance(unrolled.vert[static_cast<size_t>(i0)].P(), unrolled.vert[static_cast<size_t>(i2)].P()) >= avgR / 10.0f)
                                    continue;
                                auto nfi = vcg::tri::Allocator<VCGMesh>::AddFaces(unrolled, 1);
                                nfi->ImportData(*fi);
                                nfi->V(0) = &unrolled.vert[static_cast<size_t>(i0)];
                                nfi->V(1) = &unrolled.vert[static_cast<size_t>(i1)];
                                nfi->V(2) = &unrolled.vert[static_cast<size_t>(i2)];
                            }
                        }
                    }
                    ++loopIndex;
                }
            }

            vcg::tri::UpdateBounding<VCGMesh>::Box(unrolled);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(unrolled);
            const int ioMask = entry.ioMask;
            const int newIndex = doc.addMesh(unrolled, QObject::tr("Unrolled Mesh"), ioMask);
            if (newIndex < 0)
                return fail(QObject::tr("Failed to add unrolled mesh layer."));
            return success(true, { QObject::tr("Created unrolled mesh layer.") }, { newIndex });
        }

        if (filterId == QString::fromLatin1(kIdHalfCatmull)) {
            if (!vcg::tri::BitQuadCreation<VCGMesh>::IsTriQuadOnly(mesh)) {
                return fail(QObject::tr("Filter requires triangular and/or quad faces only."));
            }
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::BitQuadCreation<VCGMesh>::MakePureByRefine(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerBitQuadFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            markGeometry(ci, QObject::tr("Applied 4-8 subdivision on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdCatmull)) {
            PMesh baseIn, refinedOut;
            const int it = std::max(1, intParameter(parameters, QStringLiteral("Iterations"), 2));
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::PolygonSupport<VCGMesh, PMesh>::ImportFromTriMesh(baseIn, mesh);
            vcg::tri::Clean<PMesh>::RemoveUnreferencedVertex(baseIn);
            vcg::tri::Allocator<PMesh>::CompactEveryVector(baseIn);
            vcg::tri::CatmullClark<PMesh>::Refine(baseIn, refinedOut, it);
            mesh.Clear();
            vcg::tri::PolygonSupport<VCGMesh, PMesh>::ImportFromPolyMesh(mesh, refinedOut);
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerBitPolygonFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            markGeometry(ci, QObject::tr("Applied Catmull-Clark subdivision on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdDooSabin)) {
            PMesh baseIn, refinedOut;
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            if (!vcg::tri::Clean<VCGMesh>::IsFaceFauxConsistent(mesh))
                return fail(QObject::tr("Mesh has inconsistent faux-edge tagging."));
            vcg::tri::PolygonSupport<VCGMesh, PMesh>::ImportFromTriMesh(baseIn, mesh);
            vcg::tri::Clean<PMesh>::RemoveUnreferencedVertex(baseIn);
            vcg::tri::Allocator<PMesh>::CompactEveryVector(baseIn);
            vcg::tri::DooSabin<PMesh>::Refine(baseIn, refinedOut);
            mesh.Clear();
            vcg::tri::PolygonSupport<VCGMesh, PMesh>::ImportFromPolyMesh(mesh, refinedOut);
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerBitPolygonFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            markGeometry(ci, QObject::tr("Applied Doo-Sabin subdivision on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdQuadPairing)) {
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            if (vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh) > 0)
                return fail(QObject::tr("Filter requires manifoldness."));
            vcg::tri::BitQuadCreation<VCGMesh>::MakeTriEvenBySplit(mesh);
            vcg::tri::BitQuadCreation<VCGMesh>::MakePureByFlip(mesh, 100);
            vcg::tri::UpdateNormal<VCGMesh>::PerBitQuadFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            markGeometry(ci, QObject::tr("Applied tri-to-quad pairing on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdQuadDominant)) {
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            const QString lvl = enumParameter(parameters, QStringLiteral("level"), QStringLiteral("fewest"));
            int level = 0;
            if (lvl == QStringLiteral("mid"))
                level = 1;
            else if (lvl == QStringLiteral("shape"))
                level = 2;
            vcg::tri::BitQuadCreation<VCGMesh>::MakeDominant(mesh, level);
            vcg::tri::UpdateNormal<VCGMesh>::PerBitQuadFaceNormalized(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexFromCurrentFaceNormal(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            markGeometry(ci, QObject::tr("Converted '%1' to quad-dominant").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdMakePureTri)) {
            vcg::tri::BitQuadCreation<VCGMesh>::MakeBitTriOnly(mesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            markGeometry(ci, QObject::tr("Converted '%1' to pure triangular mesh").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdFauxCrease)) {
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            const float neg = float(doubleParameter(parameters, QStringLiteral("AngleDegNeg"), -45.0));
            const float pos = float(doubleParameter(parameters, QStringLiteral("AngleDegPos"), 45.0));
            vcg::tri::UpdateFlags<VCGMesh>::FaceEdgeSelSignedCrease(mesh, vcg::math::ToRad(neg), vcg::math::ToRad(pos));
            entry.ioMask |= Mask::IOM_FACEFLAGS;
            doc.markMeshMaterialChanged(ci, QObject::tr("Selected crease edges on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdFauxExtract)) {
            VCGMesh edgeMesh;
            vcg::tri::BuildFromFaceEdgeSel(mesh, edgeMesh);
            vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(edgeMesh);
            vcg::tri::UpdateBounding<VCGMesh>::Box(edgeMesh);
            const int idx = doc.addMesh(edgeMesh, QObject::tr("EdgeMesh"), Mask::IOM_EDGEINDEX);
            if (idx < 0)
                return fail(QObject::tr("Failed to create edge extraction layer."));
            return success(true, { QObject::tr("Created edge mesh from selected edges.") }, { idx });
        }

        if (filterId == QString::fromLatin1(kIdVAttrSeam)) {
            unsigned int vmask = vcg::tri::AttributeSeam::POSITION_PER_VERTEX;
            unsigned int nmask = 0;
            const QString nmode = enumParameter(parameters, QStringLiteral("NormalMode"), QStringLiteral("none"));
            if (nmode == QStringLiteral("vertex"))
                nmask |= vcg::tri::AttributeSeam::NORMAL_PER_VERTEX;
            else if (nmode == QStringLiteral("face"))
                nmask |= vcg::tri::AttributeSeam::NORMAL_PER_FACE;

            unsigned int cmask = 0;
            const QString cmode = enumParameter(parameters, QStringLiteral("ColorMode"), QStringLiteral("none"));
            if (cmode == QStringLiteral("vertex"))
                cmask |= vcg::tri::AttributeSeam::COLOR_PER_VERTEX;
            else if (cmode == QStringLiteral("face"))
                cmask |= vcg::tri::AttributeSeam::COLOR_PER_FACE;

            unsigned int tmask = 0;
            const QString tmode = enumParameter(parameters, QStringLiteral("TexcoordMode"), QStringLiteral("none"));
            if (tmode == QStringLiteral("vertex"))
                tmask |= vcg::tri::AttributeSeam::TEXCOORD_PER_VERTEX;
            else if (tmode == QStringLiteral("wedge"))
                tmask |= vcg::tri::AttributeSeam::TEXCOORD_PER_WEDGE;

            const unsigned int mask = vmask | nmask | cmask | tmask;
            if (mask == 0)
                return success(false, { QObject::tr("No attribute source selected; no changes applied.") });

            vcg::tri::AttributeSeam::ASExtract<VCGMesh, VCGMesh> vExtract(mask);
            vcg::tri::AttributeSeam::ASCompare<VCGMesh> vCompare(mask);
            const bool ok = vcg::tri::AttributeSeam::SplitVertex(mesh, vExtract, vCompare);
            if (!ok)
                return fail(QObject::tr("Failed applying Vertex Attribute Seam."));

            vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
            entry.ioMask |= Mask::IOM_VERTNORMAL | Mask::IOM_VERTCOLOR | Mask::IOM_VERTTEXCOORD;
            markGeometry(ci, QObject::tr("Applied Vertex Attribute Seam on '%1'").arg(entry.name));
            return success(true);
        }

        if (filterId == QString::fromLatin1(kIdPerimeterPolyline)) {
            if (selectedFaceCount(mesh) == 0)
                return fail(QObject::tr("No selected faces to build perimeter polyline."));

            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            VCGMesh perimeter;
            perimeter.textures = mesh.textures;

            for (auto fi = mesh.face.begin(); fi != mesh.face.end(); ++fi) {
                if (fi->IsD() || !fi->IsS())
                    continue;
                for (int ei = 0; ei < 3; ++ei) {
                    VCGFace *adjf = fi->FFp(ei);
                    if (adjf != &(*fi) && adjf && adjf->IsS())
                        continue;
                    auto eIt = vcg::tri::Allocator<VCGMesh>::AddEdges(perimeter, 1);
                    auto vIt = vcg::tri::Allocator<VCGMesh>::AddVertices(perimeter, 2);
                    vIt->P() = fi->V(ei)->P();
                    vIt->N() = fi->V(ei)->N();
                    eIt->V(0) = &(*vIt);
                    ++vIt;
                    vIt->P() = fi->V((ei + 1) % 3)->P();
                    vIt->N() = fi->V((ei + 1) % 3)->N();
                    eIt->V(1) = &(*vIt);
                }
            }

            vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(perimeter);
            vcg::tri::UpdateBounding<VCGMesh>::Box(perimeter);
            const int idx = doc.addMesh(perimeter, QObject::tr("%1_perimeter").arg(entry.name), Mask::IOM_EDGEINDEX);
            if (idx < 0)
                return fail(QObject::tr("Failed to create perimeter polyline layer."));
            return success(true, { QObject::tr("Created perimeter polyline layer.") }, { idx });
        }

        if (filterId == QString::fromLatin1(kIdSlicePlane)) {
            vcg::Point3f axis(1, 0, 0);
            const QString am = enumParameter(parameters, QStringLiteral("planeAxis"), QStringLiteral("x"));
            if (am == QStringLiteral("y"))
                axis = { 0, 1, 0 };
            else if (am == QStringLiteral("z"))
                axis = { 0, 0, 1 };
            else if (am == QStringLiteral("custom"))
                axis = {
                    float(doubleParameter(parameters, QStringLiteral("customAxisX"), 0.0)),
                    float(doubleParameter(parameters, QStringLiteral("customAxisY"), 1.0)),
                    float(doubleParameter(parameters, QStringLiteral("customAxisZ"), 0.0))
                };
            const float axn = std::sqrt(axis.SquaredNorm());
            if (axn <= 1e-20f)
                return fail(QObject::tr("Custom slicing axis must be non-zero."));
            axis /= axn;

            const float offset = float(doubleParameter(parameters, QStringLiteral("planeOffset"), 0.0));
            const QString rel = enumParameter(parameters, QStringLiteral("relativeTo"), QStringLiteral("origin"));
            vcg::Point3f planeCenter;
            if (rel == QStringLiteral("center"))
                planeCenter = mesh.bbox.Center() + axis * offset * (mesh.bbox.Diag() / 2.0f);
            else if (rel == QStringLiteral("min"))
                planeCenter = mesh.bbox.min + axis * offset * (mesh.bbox.Diag() / 2.0f);
            else
                planeCenter = axis * offset;

            vcg::Plane3f slicingPlane;
            slicingPlane.Init(planeCenter, axis);

            VCGMesh section;
            vcg::IntersectionPlaneMesh<VCGMesh, VCGMesh, VCGMesh::ScalarType>(mesh, slicingPlane, section);
            vcg::tri::Clean<VCGMesh>::RemoveDuplicateVertex(section);
            vcg::tri::UpdateBounding<VCGMesh>::Box(section);
            QVector<int> created;
            const int secIdx = doc.addMesh(section, QObject::tr("%1_sect").arg(entry.name), Mask::IOM_EDGEINDEX);
            if (secIdx >= 0)
                created.push_back(secIdx);

            if (boolParameter(parameters, QStringLiteral("createSectionSurface"), false)) {
                VCGMesh cap;
                vcg::tri::CapEdgeMesh(section, cap);
                vcg::tri::UpdateBounding<VCGMesh>::Box(cap);
                vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(cap);
                const int capIdx = doc.addMesh(cap, QObject::tr("%1_sect_filled").arg(entry.name), Mask::IOM_FACENORMAL | Mask::IOM_VERTNORMAL);
                if (capIdx >= 0)
                    created.push_back(capIdx);
            }

            if (boolParameter(parameters, QStringLiteral("splitSurfaceWithSection"), false)) {
                return fail(QObject::tr("splitSurfaceWithSection is not yet supported in QMeshLab port."));
            }

            if (created.isEmpty())
                return fail(QObject::tr("Section computation produced no output."));
            return success(true, { QObject::tr("Created %1 section layer(s).").arg(created.size()) }, created);
        }

        return fail(QObject::tr("Unknown filter id: %1").arg(filterId));
    } catch (const vcg::MissingPreconditionException &e) {
        return fail(QString::fromLocal8Bit(e.what()));
    } catch (const std::exception &e) {
        return fail(QString::fromLocal8Bit(e.what()));
    } catch (...) {
        return fail(QObject::tr("Unexpected meshing filter error."));
    }
}

void registerMeshingFilterPlugin(MeshFilterPluginManager &pluginManager)
{
    pluginManager.registerPlugin(std::make_unique<MeshingFilterPlugin>());
}
