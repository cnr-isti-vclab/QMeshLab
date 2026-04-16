#include "selectfilterplugin.h"

#include "document.h"
#include "meshfilterpluginmanager.h"
#include <QColor>
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

QColor colorParameter(const MeshFilterParameterValues &params, const QString &id, const QColor &fallback)
{
    const auto it = params.constFind(id);
    if (it == params.constEnd())
        return fallback;
    if (it.value().userType() == QMetaType::QColor) {
        const QColor c = it.value().value<QColor>();
        return c.isValid() ? c : fallback;
    }
    const QColor c(it.value().toString().trimmed());
    return c.isValid() ? c : fallback;
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

void addColorParam(
    MeshFilterDescriptor &d,
    const QString &id,
    const QString &label,
    const QString &helpMarkdown,
    const QColor &defaultValue,
    const QString &group = QStringLiteral("main"))
{
    MeshFilterParameterDescriptor p;
    p.id = id;
    p.label = label;
    p.helpMarkdown = helpMarkdown;
    p.group = group;
    p.type = MeshFilterParameterType::Color;
    p.defaultValue = defaultValue;
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

void updateGeometryAfterDeletion(VCGMesh &mesh)
{
    vcg::tri::Allocator<VCGMesh>::CompactEveryVector(mesh);
    vcg::tri::UpdateBounding<VCGMesh>::Box(mesh);
    if (mesh.FN() > 0)
        vcg::tri::UpdateNormal<VCGMesh>::PerVertexNormalizedPerFaceNormalized(mesh);
}

std::vector<MeshFilterDescriptor> buildDescriptors(const Document &doc)
{
    using Sel = vcg::tri::UpdateSelection<VCGMesh>;
    std::vector<MeshFilterDescriptor> out;

    bool defaultInvertFaces = true;
    bool defaultInvertVerts = true;
    double bboxDiag = 1.0;
    double qualityVMin = 0.0;
    double qualityVMax = 1.0;
    double qualityFMin = 0.0;
    double qualityFMax = 1.0;

    const int currentMeshIndex = doc.currentMeshIndex();
    if (currentMeshIndex >= 0 && currentMeshIndex < doc.meshCount()) {
        const VCGMesh &mesh = doc.mesh(currentMeshIndex).mesh;
        defaultInvertFaces = (Sel::FaceCount(mesh) > 0);
        defaultInvertVerts = (Sel::VertexCount(mesh) > 0);
        bboxDiag = std::max(1e-9, double(mesh.bbox.Diag()));
        if (mesh.VN() > 0) {
            const auto vRange = vcg::tri::Stat<VCGMesh>::ComputePerVertexQualityMinMax(mesh);
            qualityVMin = std::min(double(vRange.first), double(vRange.second));
            qualityVMax = std::max(double(vRange.first), double(vRange.second));
        }
        if (mesh.FN() > 0) {
            const auto fRange = vcg::tri::Stat<VCGMesh>::ComputePerFaceQualityMinMax(mesh);
            qualityFMin = std::min(double(fRange.first), double(fRange.second));
            qualityFMax = std::max(double(fRange.first), double(fRange.second));
        }
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectAll);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select All");
        d.shortDescription = QObject::tr("Select all the faces/vertices of the current mesh.");
        d.longDescriptionMarkdown = QObject::tr("Select all the faces/vertices of the current mesh.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("allFaces"),
            QObject::tr("Select all Faces"),
            QObject::tr("If true the filter will select all the faces."),
            true);
        addBoolParam(
            d,
            QStringLiteral("allVerts"),
            QObject::tr("Select all Vertices"),
            QObject::tr("If true the filter will select all the vertices."),
            true);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectNone);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select None");
        d.shortDescription = QObject::tr("Clear the current set of selected faces/vertices.");
        d.longDescriptionMarkdown =
            QObject::tr("Clear the current set of selected faces/vertices.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("allFaces"),
            QObject::tr("De-select all Faces"),
            QObject::tr("If true the filter will de-select all the faces."),
            true);
        addBoolParam(
            d,
            QStringLiteral("allVerts"),
            QObject::tr("De-select all Vertices"),
            QObject::tr("If true the filter will de-select all the vertices."),
            true);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectByAngle);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Faces by View Angle");
        d.shortDescription = QObject::tr("Select faces according to angle with view direction.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select faces according to the angle between their normal and the view direction. "
            "It is used in range map processing to select and delete steep faces parallel to "
            "view direction.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("angle"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addDoubleParam(
            d,
            QStringLiteral("anglelimit"),
            QObject::tr("Angle Threshold (deg)"),
            QObject::tr("Faces with normals at higher angle w.r.t. the view direction are selected."),
            75.0,
            0.0,
            180.0,
            3);
        addBoolParam(
            d,
            QStringLiteral("usecamera"),
            QObject::tr("Use ViewPoint from Mesh Camera"),
            QObject::tr(
                "Uses the ViewPoint from the camera associated to the current mesh. "
                "If there is no camera, an error occurs."),
            false);
        addDoubleParam(
            d,
            QStringLiteral("viewpoint_x"),
            QObject::tr("ViewPoint X"),
            QObject::tr("X coordinate of viewpoint (ignored when UseCamera is true)."),
            0.0,
            -1e9,
            1e9,
            6);
        addDoubleParam(
            d,
            QStringLiteral("viewpoint_y"),
            QObject::tr("ViewPoint Y"),
            QObject::tr("Y coordinate of viewpoint (ignored when UseCamera is true)."),
            0.0,
            -1e9,
            1e9,
            6);
        addDoubleParam(
            d,
            QStringLiteral("viewpoint_z"),
            QObject::tr("ViewPoint Z"),
            QObject::tr("Z coordinate of viewpoint (ignored when UseCamera is true)."),
            0.0,
            -1e9,
            1e9,
            6);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectUgly);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select 'Problematic' Faces");
        d.shortDescription =
            QObject::tr("Select problematic faces: elongated, flipped, or folded.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select faces with 'problems', like normal inverted w.r.t. surrounding areas, "
            "extremely elongated, or folded.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("quality"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("useAR"),
            QObject::tr("Select by Aspect Ratio"),
            QObject::tr("If true, faces with aspect ratio below the limit will be selected."),
            true);
        addDoubleParam(
            d,
            QStringLiteral("ARatio"),
            QObject::tr("Aspect Ratio"),
            QObject::tr(
                "Triangle face aspect ratio [1 (equilateral) - 0 (line)]: face is selected if "
                "below this threshold."),
            0.02,
            0.0,
            1.0,
            5);
        addBoolParam(
            d,
            QStringLiteral("useNF"),
            QObject::tr("Select by Normal Angle"),
            QObject::tr(
                "If true, adjacent faces with normals forming an angle above the limit are selected."),
            false);
        addDoubleParam(
            d,
            QStringLiteral("NFRatio"),
            QObject::tr("Angle Flip"),
            QObject::tr(
                "Angle between adjacent faces: face is selected if above this threshold."),
            60.0,
            0.0,
            180.0,
            3);
        addBoolParam(
            d,
            QStringLiteral("select_folded_faces"),
            QObject::tr("Select Folded Faces"),
            QObject::tr(
                "If true, folded faces created by quadric edge-collapse decimation are selected."),
            false);
        addDoubleParam(
            d,
            QStringLiteral("folded_faces_angle_threshold"),
            QObject::tr("Folded Faces Angle Threshold"),
            QObject::tr(
                "Angle between face normal and best-fitting plane of neighboring vertices. "
                "If above threshold, face is selected."),
            160.0,
            90.0,
            180.0,
            3);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectInvert);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Invert Selection");
        d.shortDescription = QObject::tr("Invert the current set of selected faces/vertices.");
        d.longDescriptionMarkdown =
            QObject::tr("Invert the current set of selected faces/vertices.");
        d.tags = { QStringLiteral("selection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("InvFaces"),
            QObject::tr("Invert Faces"),
            QObject::tr("If true the filter will invert the set of selected faces."),
            defaultInvertFaces);
        addBoolParam(
            d,
            QStringLiteral("InvVerts"),
            QObject::tr("Invert Vertices"),
            QObject::tr("If true the filter will invert the set of selected vertices."),
            defaultInvertVerts);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectConnected);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Connected Faces");
        d.shortDescription = QObject::tr("Expand selected faces to their connected components.");
        d.longDescriptionMarkdown = QObject::tr(
            "Expand current face selection so it includes all faces in connected components "
            "where there is at least one selected face.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("connected"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectFaceFromVert);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Faces from Vertices");
        d.shortDescription = QObject::tr("Transfer selection from selected vertices to faces.");
        d.longDescriptionMarkdown = QObject::tr("Select faces from selected vertices.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("transfer"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("Inclusive"),
            QObject::tr("Strict Selection"),
            QObject::tr(
                "If true only faces with all selected vertices are selected. "
                "Otherwise any face with at least one selected vertex is selected."),
            true);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectVertFromFace);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Vertices from Faces");
        d.shortDescription = QObject::tr("Transfer selection from selected faces to vertices.");
        d.longDescriptionMarkdown = QObject::tr("Select vertices from selected faces.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("transfer"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("Inclusive"),
            QObject::tr("Strict Selection"),
            QObject::tr(
                "If true only vertices with all incident faces selected are selected. "
                "Otherwise any vertex with at least one incident selected face is selected."),
            true);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteSelectedVerts);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete Selected Vertices");
        d.shortDescription = QObject::tr(
            "Delete selected vertices; incident faces are deleted too.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete the current set of selected vertices; faces that share one of the deleted "
            "vertices are deleted too.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("delete"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteAllFaces);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete ALL Faces");
        d.shortDescription = QObject::tr("Delete all faces, turning mesh into point cloud.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete all faces, turning the mesh into a point cloud. "
            "May be applied also to all visible layers.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("delete"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::WholeDocument;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addBoolParam(
            d,
            QStringLiteral("allLayers"),
            QObject::tr("Apply to all visible Layers"),
            QObject::tr("If true, filter is applied to all visible mesh layers."),
            false);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteSelectedFaces);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete Selected Faces");
        d.shortDescription =
            QObject::tr("Delete selected faces; unreferenced vertices are not deleted.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete the current set of selected faces, vertices that remain unreferenced are not "
            "deleted.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("delete"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterDeleteSelectedFaceVerts);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Delete Selected Faces and Vertices");
        d.shortDescription =
            QObject::tr("Delete selected faces and enclosed selected vertices.");
        d.longDescriptionMarkdown = QObject::tr(
            "Delete the current set of selected faces and all vertices surrounded by those faces.");
        d.tags = {
            QStringLiteral("selection"),
            QStringLiteral("delete"),
            QStringLiteral("face"),
            QStringLiteral("vertex")
        };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectErode);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Erode Selection");
        d.shortDescription = QObject::tr("Erode (reduce) current selected faces.");
        d.longDescriptionMarkdown =
            QObject::tr("Erode (reduce) the current set of selected faces.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("morphology"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectDilate);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Dilate Selection");
        d.shortDescription = QObject::tr("Dilate (expand) current selected faces.");
        d.longDescriptionMarkdown =
            QObject::tr("Dilate (expand) the current set of selected faces.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("morphology"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectBorder);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Border");
        d.shortDescription = QObject::tr("Select vertices and faces on mesh boundary.");
        d.longDescriptionMarkdown =
            QObject::tr("Select vertices and faces on the boundary.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("border") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectByFaceQuality);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select by Face Quality");
        d.shortDescription = QObject::tr("Select elements using per-face quality range.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select all the faces/vertices within the specified face quality range.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("quality"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireFaceQuality = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addDoubleParam(
            d,
            QStringLiteral("minQ"),
            QObject::tr("Min Quality"),
            QObject::tr("Minimum acceptable quality value."),
            qualityFMin,
            qualityFMin,
            qualityFMax,
            6);
        addDoubleParam(
            d,
            QStringLiteral("maxQ"),
            QObject::tr("Max Quality"),
            QObject::tr("Maximum acceptable quality value."),
            qualityFMax,
            qualityFMin,
            qualityFMax,
            6);
        addBoolParam(
            d,
            QStringLiteral("Inclusive"),
            QObject::tr("Inclusive Selection"),
            QObject::tr(
                "If true only vertices with all adjacent faces within range are selected. "
                "Otherwise any vertex with at least one face in range is selected."),
            true);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectByVertQuality);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select by Vertex Quality");
        d.shortDescription = QObject::tr("Select elements using per-vertex quality range.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select all the faces/vertices within the specified vertex quality range.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("quality"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.inputRequirements.requireVertexQuality = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addDoubleParam(
            d,
            QStringLiteral("minQ"),
            QObject::tr("Min Quality"),
            QObject::tr("Minimum acceptable quality value."),
            qualityVMin,
            qualityVMin,
            qualityVMax,
            6);
        addDoubleParam(
            d,
            QStringLiteral("maxQ"),
            QObject::tr("Max Quality"),
            QObject::tr("Maximum acceptable quality value."),
            qualityVMax,
            qualityVMin,
            qualityVMax,
            6);
        addBoolParam(
            d,
            QStringLiteral("Inclusive"),
            QObject::tr("Inclusive Face Selection"),
            QObject::tr(
                "If true only faces with all vertices within range are selected. "
                "Otherwise any face with at least one vertex in range is selected."),
            true);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectByColor);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Faces by Color");
        d.shortDescription = QObject::tr("Select part of the mesh based on vertex color.");
        d.longDescriptionMarkdown =
            QObject::tr("Select part of the mesh based on its color.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("color"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.inputRequirements.requireVertexColor = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addColorParam(
            d,
            QStringLiteral("Color"),
            QObject::tr("Color To Select"),
            QObject::tr("Color that you want to be selected."),
            QColor(Qt::black));
        addEnumParam(
            d,
            QStringLiteral("ColorSpace"),
            QObject::tr("Pick Color Space"),
            QObject::tr("The color space that the sliders will manipulate."),
            QStringLiteral("hsv"),
            {
                { QStringLiteral("hsv"), QStringLiteral("HSV"), {} },
                { QStringLiteral("rgb"), QStringLiteral("RGB"), {} }
            });
        addBoolParam(
            d,
            QStringLiteral("Inclusive"),
            QObject::tr("Inclusive Selection"),
            QObject::tr(
                "If true only faces with all vertices within range are selected. "
                "Otherwise any face with at least one vertex in range is selected."),
            true);
        addDoubleParam(
            d,
            QStringLiteral("PercentRH"),
            QObject::tr("Variation from Red or Hue"),
            QObject::tr(
                "A float in [0,1] representing accepted variation from selected Red/Hue."),
            0.2,
            0.0,
            1.0,
            4);
        addDoubleParam(
            d,
            QStringLiteral("PercentGS"),
            QObject::tr("Variation from Green or Saturation"),
            QObject::tr(
                "A float in [0,1] representing accepted variation from selected Green/Saturation."),
            0.2,
            0.0,
            1.0,
            4);
        addDoubleParam(
            d,
            QStringLiteral("PercentBV"),
            QObject::tr("Variation from Blue or Value"),
            QObject::tr(
                "A float in [0,1] representing accepted variation from selected Blue/Value."),
            0.2,
            0.0,
            1.0,
            4);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectSelfIntersect);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Self Intersecting Faces");
        d.shortDescription = QObject::tr("Select only self intersecting faces.");
        d.longDescriptionMarkdown =
            QObject::tr("Select only self intersecting faces.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("cleaning"), QStringLiteral("intersection") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectTexBorder);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Vertex Texture Seams");
        d.shortDescription = QObject::tr("Select vertices on texture seams.");
        d.longDescriptionMarkdown = QObject::tr("Colorize only border edges.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("texture"), QStringLiteral("seam") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.inputRequirements.requireTextureCoordinates = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectNonManifoldFace);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select non Manifold Edges");
        d.shortDescription =
            QObject::tr("Select faces and vertices incident on non manifold edges.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select faces and vertices incident on non manifold edges (e.g. edges where "
            "more than two faces are incident).");
        d.tags = { QStringLiteral("selection"), QStringLiteral("non-manifold"), QStringLiteral("edge") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectNonManifoldVertex);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select non Manifold Vertices");
        d.shortDescription = QObject::tr("Select non manifold vertices.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select non manifold vertices that do not belong to non manifold edges.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("non-manifold"), QStringLiteral("vertex") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectFacesByEdge);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Faces with Edges Longer Than...");
        d.shortDescription =
            QObject::tr("Select all triangles having an edge longer than threshold.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select all triangles having an edge with length greater or equal than a given "
            "threshold.");
        d.tags = { QStringLiteral("selection"), QStringLiteral("edge"), QStringLiteral("face") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireFaces = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addDoubleParam(
            d,
            QStringLiteral("Threshold"),
            QObject::tr("Edge Threshold"),
            QObject::tr(
                "Faces with an edge longer than this threshold will be selected."),
            bboxDiag * 0.005,
            0.0,
            bboxDiag * 0.5,
            6);
        out.push_back(std::move(d));
    }

    {
        MeshFilterDescriptor d;
        d.id = QString::fromLatin1(kFilterSelectOutlier);
        d.menuPath = QObject::tr("Selection");
        d.name = QObject::tr("Select Outliers");
        d.shortDescription = QObject::tr("Select outlier vertices using LoOP.");
        d.longDescriptionMarkdown = QObject::tr(
            "Select vertices classified as outliers using Local Outlier Probability "
            "described in: **LoOP: Local Outlier Probabilities** (Kriegel et al., CIKM 2009).");
        d.tags = { QStringLiteral("selection"), QStringLiteral("outlier"), QStringLiteral("point cloud") };
        d.inputDomain = MeshFilterInputDomain::SingleMesh;
        d.inputRequirements.requireVertices = true;
        d.outputDomain = MeshFilterOutputDomain::ModifyCurrentMesh;
        addDoubleParam(
            d,
            QStringLiteral("PropThreshold"),
            QObject::tr("Probability"),
            QObject::tr(
                "Threshold to select a vertex. Vertex is selected if LoOP value is above threshold."),
            0.8,
            0.0,
            1.0,
            4);
        addIntParam(
            d,
            QStringLiteral("KNearest"),
            QObject::tr("Number of neighbors"),
            QObject::tr("Number of neighbors used to compute LoOP."),
            32,
            1,
            1000000);
        out.push_back(std::move(d));
    }

    return out;
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

std::vector<MeshFilterDescriptor> SelectFilterPlugin::filters(const Document &doc) const
{
    return buildDescriptors(doc);
}

MeshFilterRunResult SelectFilterPlugin::runFilter(
    const QString &filterId,
    const MeshFilterParameterValues &parameters,
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
        doc.markMeshMaterialChanged(meshIndex, changeMsg);
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
        if (boolParameter(parameters, QStringLiteral("allVerts"), true))
            Sel::VertexAll(mesh);
        if (boolParameter(parameters, QStringLiteral("allFaces"), true))
            Sel::FaceAll(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Select all on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectNone)) {
        if (boolParameter(parameters, QStringLiteral("allVerts"), true))
            Sel::VertexClear(mesh);
        if (boolParameter(parameters, QStringLiteral("allFaces"), true))
            Sel::FaceClear(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Clear selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectByAngle)) {
        if (mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh has no faces."));

        if (boolParameter(parameters, QStringLiteral("usecamera"), false)) {
            return fail(QObject::tr(
                "Use ViewPoint from Mesh Camera is not supported in current QMeshLab data model."));
        }

        vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(mesh);
        const vcg::Point3f viewpoint(
            float(doubleParameter(parameters, QStringLiteral("viewpoint_x"), 0.0)),
            float(doubleParameter(parameters, QStringLiteral("viewpoint_y"), 0.0)),
            float(doubleParameter(parameters, QStringLiteral("viewpoint_z"), 0.0)));
        const float angleDeg = float(doubleParameter(parameters, QStringLiteral("anglelimit"), 75.0));
        const float limit = std::cos(vcg::math::ToRad(angleDeg));

        int selected = 0;
        for (VCGFace &f : mesh.face) {
            if (f.IsD())
                continue;
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

        if (boolParameter(parameters, QStringLiteral("useAR"), true)) {
            const float aRatio = float(doubleParameter(parameters, QStringLiteral("ARatio"), 0.02));
            for (VCGFace &f : mesh.face) {
                if (f.IsD())
                    continue;
                const float q = vcg::QualityRadii(f.V(0)->P(), f.V(1)->P(), f.V(2)->P());
                if (q < aRatio) {
                    if (!f.IsS())
                        ++selectedByAR;
                    f.SetS();
                }
            }
        }

        if (boolParameter(parameters, QStringLiteral("useNF"), false)) {
            vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
            vcg::tri::UpdateNormal<VCGMesh>::PerFaceNormalized(mesh);
            const float nfRatio = float(doubleParameter(parameters, QStringLiteral("NFRatio"), 60.0));
            for (VCGFace &f : mesh.face) {
                if (f.IsD())
                    continue;
                float worstAngle = 0.0f;
                for (int ei = 0; ei < 3; ++ei) {
                    VCGFace *adjf = f.FFp(ei);
                    if (!adjf || adjf == &f || adjf->IsD())
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

        if (boolParameter(parameters, QStringLiteral("select_folded_faces"), false)) {
            const float angleThr =
                float(doubleParameter(parameters, QStringLiteral("folded_faces_angle_threshold"), 160.0));
            const int beforeSel = int(Sel::FaceCount(mesh));
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
        if (boolParameter(parameters, QStringLiteral("InvVerts"), true))
            Sel::VertexInvert(mesh);
        if (boolParameter(parameters, QStringLiteral("InvFaces"), true))
            Sel::FaceInvert(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Invert selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectConnected)) {
        if (mesh.FN() <= 0)
            return fail(QObject::tr("Current mesh has no faces."));
        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
        Sel::FaceConnectedFF(mesh);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Expanded connected face selection on '%1'").arg(entry.name));
    }

    if (filterId == QString::fromLatin1(kFilterSelectFaceFromVert)) {
        const bool strict = boolParameter(parameters, QStringLiteral("Inclusive"), true);
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
        const bool strict = boolParameter(parameters, QStringLiteral("Inclusive"), true);
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
            if (!f.IsD() && f.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, f);
        }
        for (VCGVertex &v : mesh.vert) {
            if (!v.IsD() && v.IsS())
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
        const bool allLayers = boolParameter(parameters, QStringLiteral("allLayers"), false);
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
                    if (!f.IsD())
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
                    if (!f.IsD())
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
            if (!f.IsD() && f.IsS())
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
            if (!f.IsD() && f.IsS())
                vcg::tri::Allocator<VCGMesh>::DeleteFace(mesh, f);
        }
        for (VCGVertex &v : mesh.vert) {
            if (!v.IsD() && v.IsS())
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
        const float minQ = float(doubleParameter(parameters, QStringLiteral("minQ"), 0.0));
        const float maxQ = float(doubleParameter(parameters, QStringLiteral("maxQ"), 1.0));
        const bool inclusive = boolParameter(parameters, QStringLiteral("Inclusive"), true);
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
        const float minQ = float(doubleParameter(parameters, QStringLiteral("minQ"), 0.0));
        const float maxQ = float(doubleParameter(parameters, QStringLiteral("maxQ"), 1.0));
        const bool inclusive = boolParameter(parameters, QStringLiteral("Inclusive"), true);
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
        const QColor targetColor = colorParameter(parameters, QStringLiteral("Color"), QColor(Qt::black));
        const QString colorSpace =
            enumParameter(parameters, QStringLiteral("ColorSpace"), QStringLiteral("hsv")).toLower();
        const bool inclusive = boolParameter(parameters, QStringLiteral("Inclusive"), true);
        const float valueRH = float(doubleParameter(parameters, QStringLiteral("PercentRH"), 0.2));
        const float valueGS = float(doubleParameter(parameters, QStringLiteral("PercentGS"), 0.2));
        const float valueBV = float(doubleParameter(parameters, QStringLiteral("PercentBV"), 0.2));

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
            if (v.IsD())
                continue;
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
        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
        const int nm = vcg::tri::Clean<VCGMesh>::CountNonManifoldEdgeFF(mesh, true);
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected non manifold edges on '%1'").arg(entry.name),
            { QObject::tr("Non manifold edges found: %1").arg(nm) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectNonManifoldVertex)) {
        vcg::tri::UpdateTopology<VCGMesh>::FaceFace(mesh);
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
            if (f && !f->IsD())
                f->SetS();
        }
        return selectionResult(
            meshIndex,
            entry,
            QObject::tr("Selected self intersecting faces on '%1'").arg(entry.name),
            { QObject::tr("Self intersecting faces: %1").arg(int(intersFaces.size())) });
    }

    if (filterId == QString::fromLatin1(kFilterSelectFacesByEdge)) {
        const float threshold = float(doubleParameter(parameters, QStringLiteral("Threshold"), mesh.bbox.Diag() * 0.005));
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
        const float threshold = float(doubleParameter(parameters, QStringLiteral("PropThreshold"), 0.8));
        const int kNearest = std::max(1, intParameter(parameters, QStringLiteral("KNearest"), 32));
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
